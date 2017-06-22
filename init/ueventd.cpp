/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ueventd.h"

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <set>
#include <thread>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <selinux/android.h>
#include <selinux/selinux.h>

#include "devices.h"
#include "firmware_handler.h"
#include "log.h"
#include "uevent_listener.h"
#include "ueventd_parser.h"
#include "util.h"

// At a high level, ueventd listens for uevent messages generated by the kernel through a netlink
// socket.  When ueventd receives such a message it handles it by taking appropriate actions,
// which can typically be creating a device node in /dev, setting file permissions, setting selinux
// labels, etc.
// Ueventd also handles loading of firmware that the kernel requests, and creates symlinks for block
// and character devices.

// When ueventd starts, it regenerates uevents for all currently registered devices by traversing
// /sys and writing 'add' to each 'uevent' file that it finds.  This causes the kernel to generate
// and resend uevent messages for all of the currently registered devices.  This is done, because
// ueventd would not have been running when these devices were registered and therefore was unable
// to receive their uevent messages and handle them appropriately.  This process is known as
// 'cold boot'.

// 'init' currently waits synchronously on the cold boot process of ueventd before it continues
// its boot process.  For this reason, cold boot should be as quick as possible.  One way to achieve
// a speed up here is to parallelize the handling of ueventd messages, which consume the bulk of the
// time during cold boot.

// Handling of uevent messages has two unique properties:
// 1) It can be done in isolation; it doesn't need to read or write any status once it is started.
// 2) It uses setegid() and setfscreatecon() so either care (aka locking) must be taken to ensure
//    that no file system operations are done while the uevent process has an abnormal egid or
//    fscreatecon or this handling must happen in a separate process.
// Given the above two properties, it is best to fork() subprocesses to handle the uevents.  This
// reduces the overhead and complexity that would be required in a solution with threads and locks.
// In testing, a racy multithreaded solution has the same performance as the fork() solution, so
// there is no reason to deal with the complexity of the former.

// One other important caveat during the boot process is the handling of SELinux restorecon.
// Since many devices have child devices, calling selinux_android_restorecon() recursively for each
// device when its uevent is handled, results in multiple restorecon operations being done on a
// given file.  It is more efficient to simply do restorecon recursively on /sys during cold boot,
// than to do restorecon on each device as its uevent is handled.  This only applies to cold boot;
// once that has completed, restorecon is done for each device as its uevent is handled.

// With all of the above considered, the cold boot process has the below steps:
// 1) ueventd regenerates uevents by doing the /sys traversal and listens to the netlink socket for
//    the generated uevents.  It writes these uevents into a queue represented by a vector.
//
// 2) ueventd forks 'n' separate uevent handler subprocesses and has each of them to handle the
//    uevents in the queue based on a starting offset (their process number) and a stride (the total
//    number of processes).  Note that no IPC happens at this point and only const functions from
//    DeviceHandler should be called from this context.
//
// 3) In parallel to the subprocesses handling the uevents, the main thread of ueventd calls
//    selinux_android_restorecon() recursively on /sys/class, /sys/block, and /sys/devices.
//
// 4) Once the restorecon operation finishes, the main thread calls waitpid() to wait for all
//    subprocess handlers to complete and exit.  Once this happens, it marks coldboot as having
//    completed.
//
// At this point, ueventd is single threaded, poll()'s and then handles any future uevents.

// Lastly, it should be noted that uevents that occur during the coldboot process are handled
// without issue after the coldboot process completes.  This is because the uevent listener is
// paused while the uevent handler and restorecon actions take place.  Once coldboot completes,
// the uevent listener resumes in polling mode and will handle the uevents that occurred during
// coldboot.

class ColdBoot {
  public:
    ColdBoot(UeventListener& uevent_listener, DeviceHandler& device_handler)
        : uevent_listener_(uevent_listener),
          device_handler_(device_handler),
          num_handler_subprocesses_(std::thread::hardware_concurrency() ?: 4) {}

    void Run();

  private:
    void UeventHandlerMain(unsigned int process_num, unsigned int total_processes);
    void RegenerateUevents();
    void ForkSubProcesses();
    void DoRestoreCon();
    void WaitForSubProcesses();

    UeventListener& uevent_listener_;
    DeviceHandler& device_handler_;

    unsigned int num_handler_subprocesses_;
    std::vector<Uevent> uevent_queue_;

    std::set<pid_t> subprocess_pids_;
};

void ColdBoot::UeventHandlerMain(unsigned int process_num, unsigned int total_processes) {
    for (unsigned int i = process_num; i < uevent_queue_.size(); i += total_processes) {
        auto& uevent = uevent_queue_[i];
        device_handler_.HandleDeviceEvent(uevent);
    }
    _exit(EXIT_SUCCESS);
}

void ColdBoot::RegenerateUevents() {
    uevent_listener_.RegenerateUevents([this](const Uevent& uevent) {
        HandleFirmwareEvent(uevent);

        uevent_queue_.emplace_back(std::move(uevent));
        return ListenerAction::kContinue;
    });
}

void ColdBoot::ForkSubProcesses() {
    for (unsigned int i = 0; i < num_handler_subprocesses_; ++i) {
        auto pid = fork();
        if (pid < 0) {
            PLOG(FATAL) << "fork() failed!";
        }

        if (pid == 0) {
            UeventHandlerMain(i, num_handler_subprocesses_);
        }

        subprocess_pids_.emplace(pid);
    }
}

void ColdBoot::DoRestoreCon() {
    selinux_android_restorecon("/sys", SELINUX_ANDROID_RESTORECON_RECURSE);
    device_handler_.set_skip_restorecon(false);
}

void ColdBoot::WaitForSubProcesses() {
    // Treat subprocesses that crash or get stuck the same as if ueventd itself has crashed or gets
    // stuck.
    //
    // When a subprocess crashes, we fatally abort from ueventd.  init will restart ueventd when
    // init reaps it, and the cold boot process will start again.  If this continues to fail, then
    // since ueventd is marked as a critical service, init will reboot to recovery.
    //
    // When a subprocess gets stuck, keep ueventd spinning waiting for it.  init has a timeout for
    // cold boot and will reboot to the bootloader if ueventd does not complete in time.
    while (!subprocess_pids_.empty()) {
        int status;
        pid_t pid = TEMP_FAILURE_RETRY(waitpid(-1, &status, 0));
        if (pid == -1) {
            PLOG(ERROR) << "waitpid() failed";
            continue;
        }

        auto it = std::find(subprocess_pids_.begin(), subprocess_pids_.end(), pid);
        if (it == subprocess_pids_.end()) continue;

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == EXIT_SUCCESS) {
                subprocess_pids_.erase(it);
            } else {
                LOG(FATAL) << "subprocess exited with status " << WEXITSTATUS(status);
            }
        } else if (WIFSIGNALED(status)) {
            LOG(FATAL) << "subprocess killed by signal " << WTERMSIG(status);
        }
    }
}

void ColdBoot::Run() {
    Timer cold_boot_timer;

    RegenerateUevents();

    ForkSubProcesses();

    DoRestoreCon();

    WaitForSubProcesses();

    close(open(COLDBOOT_DONE, O_WRONLY | O_CREAT | O_CLOEXEC, 0000));
    LOG(INFO) << "Coldboot took " << cold_boot_timer;
}

DeviceHandler CreateDeviceHandler() {
    Parser parser;

    std::vector<Subsystem> subsystems;
    parser.AddSectionParser("subsystem", std::make_unique<SubsystemParser>(&subsystems));

    using namespace std::placeholders;
    std::vector<SysfsPermissions> sysfs_permissions;
    std::vector<Permissions> dev_permissions;
    parser.AddSingleLineParser(
        "/sys/", std::bind(ParsePermissionsLine, _1, _2, &sysfs_permissions, nullptr));
    parser.AddSingleLineParser("/dev/",
                               std::bind(ParsePermissionsLine, _1, _2, nullptr, &dev_permissions));

    parser.ParseConfig("/ueventd.rc");
    parser.ParseConfig("/vendor/ueventd.rc");
    parser.ParseConfig("/odm/ueventd.rc");

    /*
     * keep the current product name base configuration so
     * we remain backwards compatible and allow it to override
     * everything
     * TODO: cleanup platform ueventd.rc to remove vendor specific
     * device node entries (b/34968103)
     */
    std::string hardware = android::base::GetProperty("ro.hardware", "");
    parser.ParseConfig("/ueventd." + hardware + ".rc");

    return DeviceHandler(std::move(dev_permissions), std::move(sysfs_permissions),
                         std::move(subsystems), true);
}

int ueventd_main(int argc, char** argv) {
    /*
     * init sets the umask to 077 for forked processes. We need to
     * create files with exact permissions, without modification by
     * the umask.
     */
    umask(000);

    InitKernelLogging(argv);

    LOG(INFO) << "ueventd started!";

    selinux_callback cb;
    cb.func_log = selinux_klog_callback;
    selinux_set_callback(SELINUX_CB_LOG, cb);

    DeviceHandler device_handler = CreateDeviceHandler();
    UeventListener uevent_listener;

    if (access(COLDBOOT_DONE, F_OK) != 0) {
        ColdBoot cold_boot(uevent_listener, device_handler);
        cold_boot.Run();
    }

    uevent_listener.Poll([&device_handler](const Uevent& uevent) {
        HandleFirmwareEvent(uevent);
        device_handler.HandleDeviceEvent(uevent);
        return ListenerAction::kContinue;
    });

    return 0;
}
