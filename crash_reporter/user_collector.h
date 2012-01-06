// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CRASH_REPORTER_USER_COLLECTOR_H_
#define _CRASH_REPORTER_USER_COLLECTOR_H_

#include <string>
#include <vector>

#include "crash-reporter/crash_collector.h"
#include "gtest/gtest_prod.h"  // for FRIEND_TEST

class FilePath;
class SystemLogging;

// User crash collector.
class UserCollector : public CrashCollector {
 public:
  UserCollector();

  // Initialize the user crash collector for detection of crashes,
  // given a crash counting function, the path to this executable,
  // metrics collection enabled oracle, and system logger facility.
  // Crash detection/reporting is not enabled until Enable is called.
  // |generate_diagnostics| is used to indicate whether or not to try
  // to generate a minidump from crashes.
  void Initialize(CountCrashFunction count_crash,
                  const std::string &our_path,
                  IsFeedbackAllowedFunction is_metrics_allowed,
                  bool generate_diagnostics);

  virtual ~UserCollector();

  // Enable collection.
  bool Enable() { return SetUpInternal(true); }

  // Disable collection.
  bool Disable() { return SetUpInternal(false); }

  // Handle a specific user crash.  Returns true on success.
  bool HandleCrash(const std::string &crash_attributes,
                   const char *force_exec);

  // Set (override the default) core file pattern.
  void set_core_pattern_file(const std::string &pattern) {
    core_pattern_file_ = pattern;
  }

  // Set (override the default) core pipe limit file.
  void set_core_pipe_limit_file(const std::string &path) {
    core_pipe_limit_file_ = path;
  }

 private:
  friend class UserCollectorTest;
  FRIEND_TEST(UserCollectorTest, CopyOffProcFilesBadPath);
  FRIEND_TEST(UserCollectorTest, CopyOffProcFilesBadPid);
  FRIEND_TEST(UserCollectorTest, CopyOffProcFilesOK);
  FRIEND_TEST(UserCollectorTest, GetExecutableBaseNameFromPid);
  FRIEND_TEST(UserCollectorTest, GetFirstLineWithPrefix);
  FRIEND_TEST(UserCollectorTest, GetIdFromStatus);
  FRIEND_TEST(UserCollectorTest, GetStateFromStatus);
  FRIEND_TEST(UserCollectorTest, GetProcessPath);
  FRIEND_TEST(UserCollectorTest, GetSymlinkTarget);
  FRIEND_TEST(UserCollectorTest, GetUserInfoFromName);
  FRIEND_TEST(UserCollectorTest, ParseCrashAttributes);
  FRIEND_TEST(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage);
  FRIEND_TEST(UserCollectorTest, ShouldDumpDeveloperImageOverridesConsent);
  FRIEND_TEST(UserCollectorTest, ShouldDumpUseConsentProductionImage);
  FRIEND_TEST(UserCollectorTest, ValidateProcFiles);

  // Enumeration to pass to GetIdFromStatus.  Must match the order
  // that the kernel lists IDs in the status file.
  enum IdKind {
    kIdReal = 0,  // uid and gid
    kIdEffective = 1,  // euid and egid
    kIdSet = 2,  // suid and sgid
    kIdFileSystem = 3,  // fsuid and fsgid
    kIdMax
  };

  static const int kForkProblem = 255;

  std::string GetPattern(bool enabled) const;
  bool SetUpInternal(bool enabled);

  FilePath GetProcessPath(pid_t pid);
  bool GetSymlinkTarget(const FilePath &symlink,
                        FilePath *target);
  bool GetExecutableBaseNameFromPid(uid_t pid,
                                    std::string *base_name);
  // Returns, via |line|, the first line in |lines| that starts with |prefix|.
  // Returns true if a line is found, or false otherwise.
  bool GetFirstLineWithPrefix(const std::vector<std::string> &lines,
                              const char *prefix, std::string *line);

  // Returns the identifier of |kind|, via |id|, found in |status_lines| on
  // the line starting with |prefix|. |status_lines| contains the lines in
  // the status file. Returns true if the identifier can be determined.
  bool GetIdFromStatus(const char *prefix,
                       IdKind kind,
                       const std::vector<std::string> &status_lines,
                       int *id);

  // Returns the process state, via |state|, found in |status_lines|, which
  // contains the lines in the status file. Returns true if the process state
  // can be determined.
  bool GetStateFromStatus(const std::vector<std::string> &status_lines,
                          std::string *state);

  void LogCollectionError(const std::string &error_message);
  void EnqueueCollectionErrorLog(pid_t pid, const std::string &exec_name);

  bool CopyOffProcFiles(pid_t pid, const FilePath &container_dir);

  // Validates the proc files at |container_dir| and returns true if they
  // are usable for the core-to-minidump conversion later. For instance, if
  // a process is reaped by the kernel before the copying of its proc files
  // takes place, some proc files like /proc/<pid>/maps may contain nothing
  // and thus become unusable.
  bool ValidateProcFiles(const FilePath &container_dir);

  // Determines the crash directory for given pid based on pid's owner,
  // and creates the directory if necessary with appropriate permissions.
  // Returns true whether or not directory needed to be created, false on
  // any failure.
  bool GetCreatedCrashDirectory(pid_t pid,
                                FilePath *crash_file_path,
                                bool *out_of_capacity);
  bool CopyStdinToCoreFile(const FilePath &core_path);
  bool RunCoreToMinidump(const FilePath &core_path,
                         const FilePath &procfs_directory,
                         const FilePath &minidump_path,
                         const FilePath &temp_directory);
  bool ConvertCoreToMinidump(pid_t pid,
                             const FilePath &container_dir,
                             const FilePath &core_path,
                             const FilePath &minidump_path);
  bool ConvertAndEnqueueCrash(int pid, const std::string &exec_name,
                              bool *out_of_capacity);
  bool ParseCrashAttributes(const std::string &crash_attributes,
                            pid_t *pid, int *signal,
                            std::string *kernel_supplied_name);

  bool ShouldDump(bool has_owner_consent,
                  bool is_developer,
                  bool handle_chrome_crashes,
                  const std::string &exec,
                  std::string *reason);

  bool generate_diagnostics_;
  std::string core_pattern_file_;
  std::string core_pipe_limit_file_;
  std::string our_path_;
  bool initialized_;

  static const char *kUserId;
  static const char *kGroupId;
};

#endif  // _CRASH_REPORTER_USER_COLLECTOR_H_
