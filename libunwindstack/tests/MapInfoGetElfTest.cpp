/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <android-base/file.h>
#include <android-base/test_utils.h>
#include <gtest/gtest.h>

#include <unwindstack/Elf.h>
#include <unwindstack/MapInfo.h>
#include <unwindstack/Maps.h>
#include <unwindstack/Memory.h>

#include "ElfTestUtils.h"
#include "MemoryFake.h"

namespace unwindstack {

class MapInfoGetElfTest : public ::testing::Test {
 protected:
  void SetUp() override {
    memory_ = new MemoryFake;
    process_memory_.reset(memory_);
  }

  template <typename Ehdr, typename Shdr>
  static void InitElf(uint64_t sh_offset, Ehdr* ehdr, uint8_t class_type, uint8_t machine_type) {
    memset(ehdr, 0, sizeof(*ehdr));
    memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
    ehdr->e_ident[EI_CLASS] = class_type;
    ehdr->e_machine = machine_type;
    ehdr->e_shoff = sh_offset;
    ehdr->e_shentsize = sizeof(Shdr) + 100;
    ehdr->e_shnum = 4;
  }

  const size_t kMapSize = 4096;

  std::shared_ptr<Memory> process_memory_;
  MemoryFake* memory_;

  TemporaryFile elf_;
};

TEST_F(MapInfoGetElfTest, invalid) {
  MapInfo info{.start = 0x1000, .end = 0x2000, .offset = 0, .flags = PROT_READ, .name = ""};

  // The map is empty, but this should still create an invalid elf object.
  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_FALSE(elf->valid());
}

TEST_F(MapInfoGetElfTest, valid32) {
  MapInfo info{.start = 0x3000, .end = 0x4000, .offset = 0, .flags = PROT_READ, .name = ""};

  Elf32_Ehdr ehdr;
  TestInitEhdr<Elf32_Ehdr>(&ehdr, ELFCLASS32, EM_ARM);
  memory_->SetMemory(0x3000, &ehdr, sizeof(ehdr));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_ARM), elf->machine_type());
  EXPECT_EQ(ELFCLASS32, elf->class_type());
}

TEST_F(MapInfoGetElfTest, valid64) {
  MapInfo info{.start = 0x8000, .end = 0x9000, .offset = 0, .flags = PROT_READ, .name = ""};

  Elf64_Ehdr ehdr;
  TestInitEhdr<Elf64_Ehdr>(&ehdr, ELFCLASS64, EM_AARCH64);
  memory_->SetMemory(0x8000, &ehdr, sizeof(ehdr));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_AARCH64), elf->machine_type());
  EXPECT_EQ(ELFCLASS64, elf->class_type());
}

TEST_F(MapInfoGetElfTest, gnu_debugdata_do_not_init32) {
  MapInfo info{.start = 0x4000, .end = 0x8000, .offset = 0, .flags = PROT_READ, .name = ""};

  TestInitGnuDebugdata<Elf32_Ehdr, Elf32_Shdr>(ELFCLASS32, EM_ARM, false,
                                               [&](uint64_t offset, const void* ptr, size_t size) {
                                                 memory_->SetMemory(0x4000 + offset, ptr, size);
                                               });

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_ARM), elf->machine_type());
  EXPECT_EQ(ELFCLASS32, elf->class_type());
  EXPECT_TRUE(elf->gnu_debugdata_interface() == nullptr);
}

TEST_F(MapInfoGetElfTest, gnu_debugdata_do_not_init64) {
  MapInfo info{.start = 0x6000, .end = 0x8000, .offset = 0, .flags = PROT_READ, .name = ""};

  TestInitGnuDebugdata<Elf64_Ehdr, Elf64_Shdr>(ELFCLASS64, EM_AARCH64, false,
                                               [&](uint64_t offset, const void* ptr, size_t size) {
                                                 memory_->SetMemory(0x6000 + offset, ptr, size);
                                               });

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_AARCH64), elf->machine_type());
  EXPECT_EQ(ELFCLASS64, elf->class_type());
  EXPECT_TRUE(elf->gnu_debugdata_interface() == nullptr);
}

TEST_F(MapInfoGetElfTest, gnu_debugdata_init32) {
  MapInfo info{.start = 0x2000, .end = 0x3000, .offset = 0, .flags = PROT_READ, .name = ""};

  TestInitGnuDebugdata<Elf32_Ehdr, Elf32_Shdr>(ELFCLASS32, EM_ARM, true,
                                               [&](uint64_t offset, const void* ptr, size_t size) {
                                                 memory_->SetMemory(0x2000 + offset, ptr, size);
                                               });

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, true));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_ARM), elf->machine_type());
  EXPECT_EQ(ELFCLASS32, elf->class_type());
  EXPECT_TRUE(elf->gnu_debugdata_interface() != nullptr);
}

TEST_F(MapInfoGetElfTest, gnu_debugdata_init64) {
  MapInfo info{.start = 0x5000, .end = 0x8000, .offset = 0, .flags = PROT_READ, .name = ""};

  TestInitGnuDebugdata<Elf64_Ehdr, Elf64_Shdr>(ELFCLASS64, EM_AARCH64, true,
                                               [&](uint64_t offset, const void* ptr, size_t size) {
                                                 memory_->SetMemory(0x5000 + offset, ptr, size);
                                               });

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, true));
  ASSERT_TRUE(elf.get() != nullptr);
  ASSERT_TRUE(elf->valid());
  EXPECT_EQ(static_cast<uint32_t>(EM_AARCH64), elf->machine_type());
  EXPECT_EQ(ELFCLASS64, elf->class_type());
  EXPECT_TRUE(elf->gnu_debugdata_interface() != nullptr);
}

TEST_F(MapInfoGetElfTest, end_le_start) {
  MapInfo info{.start = 0x1000, .end = 0x1000, .offset = 0, .flags = PROT_READ, .name = elf_.path};

  Elf32_Ehdr ehdr;
  TestInitEhdr<Elf32_Ehdr>(&ehdr, ELFCLASS32, EM_ARM);
  ASSERT_TRUE(android::base::WriteFully(elf_.fd, &ehdr, sizeof(ehdr)));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_FALSE(elf->valid());

  info.elf = nullptr;
  info.end = 0xfff;
  elf.reset(info.GetElf(process_memory_, false));
  ASSERT_FALSE(elf->valid());

  // Make sure this test is valid.
  info.elf = nullptr;
  info.end = 0x2000;
  elf.reset(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
}

// Verify that if the offset is non-zero but there is no elf at the offset,
// that the full file is used.
TEST_F(MapInfoGetElfTest, file_backed_non_zero_offset_full_file) {
  MapInfo info{
      .start = 0x1000, .end = 0x2000, .offset = 0x100, .flags = PROT_READ, .name = elf_.path};

  std::vector<uint8_t> buffer(0x1000);
  memset(buffer.data(), 0, buffer.size());
  Elf32_Ehdr ehdr;
  TestInitEhdr<Elf32_Ehdr>(&ehdr, ELFCLASS32, EM_ARM);
  memcpy(buffer.data(), &ehdr, sizeof(ehdr));
  ASSERT_TRUE(android::base::WriteFully(elf_.fd, buffer.data(), buffer.size()));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
  ASSERT_TRUE(elf->memory() != nullptr);
  ASSERT_EQ(0x100U, info.elf_offset);

  // Read the entire file.
  memset(buffer.data(), 0, buffer.size());
  ASSERT_TRUE(elf->memory()->ReadFully(0, buffer.data(), buffer.size()));
  ASSERT_EQ(0, memcmp(buffer.data(), &ehdr, sizeof(ehdr)));
  for (size_t i = sizeof(ehdr); i < buffer.size(); i++) {
    ASSERT_EQ(0, buffer[i]) << "Failed at byte " << i;
  }

  ASSERT_FALSE(elf->memory()->ReadFully(buffer.size(), buffer.data(), 1));
}

// Verify that if the offset is non-zero and there is an elf at that
// offset, that only part of the file is used.
TEST_F(MapInfoGetElfTest, file_backed_non_zero_offset_partial_file) {
  MapInfo info{
      .start = 0x1000, .end = 0x2000, .offset = 0x2000, .flags = PROT_READ, .name = elf_.path};

  std::vector<uint8_t> buffer(0x4000);
  memset(buffer.data(), 0, buffer.size());
  Elf32_Ehdr ehdr;
  TestInitEhdr<Elf32_Ehdr>(&ehdr, ELFCLASS32, EM_ARM);
  memcpy(&buffer[info.offset], &ehdr, sizeof(ehdr));
  ASSERT_TRUE(android::base::WriteFully(elf_.fd, buffer.data(), buffer.size()));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
  ASSERT_TRUE(elf->memory() != nullptr);
  ASSERT_EQ(0U, info.elf_offset);

  // Read the valid part of the file.
  ASSERT_TRUE(elf->memory()->ReadFully(0, buffer.data(), 0x1000));
  ASSERT_EQ(0, memcmp(buffer.data(), &ehdr, sizeof(ehdr)));
  for (size_t i = sizeof(ehdr); i < 0x1000; i++) {
    ASSERT_EQ(0, buffer[i]) << "Failed at byte " << i;
  }

  ASSERT_FALSE(elf->memory()->ReadFully(0x1000, buffer.data(), 1));
}

// Verify that if the offset is non-zero and there is an elf at that
// offset, that only part of the file is used. Further verify that if the
// embedded elf is bigger than the initial map, the new object is larger
// than the original map size. Do this for a 32 bit elf and a 64 bit elf.
TEST_F(MapInfoGetElfTest, file_backed_non_zero_offset_partial_file_whole_elf32) {
  MapInfo info{
      .start = 0x5000, .end = 0x6000, .offset = 0x1000, .flags = PROT_READ, .name = elf_.path};

  std::vector<uint8_t> buffer(0x4000);
  memset(buffer.data(), 0, buffer.size());
  Elf32_Ehdr ehdr;
  TestInitEhdr<Elf32_Ehdr>(&ehdr, ELFCLASS32, EM_ARM);
  ehdr.e_shoff = 0x2000;
  ehdr.e_shentsize = sizeof(Elf32_Shdr) + 100;
  ehdr.e_shnum = 4;
  memcpy(&buffer[info.offset], &ehdr, sizeof(ehdr));
  ASSERT_TRUE(android::base::WriteFully(elf_.fd, buffer.data(), buffer.size()));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
  ASSERT_TRUE(elf->memory() != nullptr);
  ASSERT_EQ(0U, info.elf_offset);

  // Verify the memory is a valid elf.
  memset(buffer.data(), 0, buffer.size());
  ASSERT_TRUE(elf->memory()->ReadFully(0, buffer.data(), 0x1000));
  ASSERT_EQ(0, memcmp(buffer.data(), &ehdr, sizeof(ehdr)));

  // Read past the end of what would normally be the size of the map.
  ASSERT_TRUE(elf->memory()->ReadFully(0x1000, buffer.data(), 1));
}

TEST_F(MapInfoGetElfTest, file_backed_non_zero_offset_partial_file_whole_elf64) {
  MapInfo info{
      .start = 0x7000, .end = 0x8000, .offset = 0x1000, .flags = PROT_READ, .name = elf_.path};

  std::vector<uint8_t> buffer(0x4000);
  memset(buffer.data(), 0, buffer.size());
  Elf64_Ehdr ehdr;
  TestInitEhdr<Elf64_Ehdr>(&ehdr, ELFCLASS64, EM_AARCH64);
  ehdr.e_shoff = 0x2000;
  ehdr.e_shentsize = sizeof(Elf64_Shdr) + 100;
  ehdr.e_shnum = 4;
  memcpy(&buffer[info.offset], &ehdr, sizeof(ehdr));
  ASSERT_TRUE(android::base::WriteFully(elf_.fd, buffer.data(), buffer.size()));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
  ASSERT_TRUE(elf->memory() != nullptr);
  ASSERT_EQ(0U, info.elf_offset);

  // Verify the memory is a valid elf.
  memset(buffer.data(), 0, buffer.size());
  ASSERT_TRUE(elf->memory()->ReadFully(0, buffer.data(), 0x1000));
  ASSERT_EQ(0, memcmp(buffer.data(), &ehdr, sizeof(ehdr)));

  // Read past the end of what would normally be the size of the map.
  ASSERT_TRUE(elf->memory()->ReadFully(0x1000, buffer.data(), 1));
}

TEST_F(MapInfoGetElfTest, process_memory_not_read_only) {
  MapInfo info{.start = 0x9000, .end = 0xa000, .offset = 0x1000, .flags = 0, .name = ""};

  // Create valid elf data in process memory only.
  Elf64_Ehdr ehdr;
  TestInitEhdr<Elf64_Ehdr>(&ehdr, ELFCLASS64, EM_AARCH64);
  ehdr.e_shoff = 0x2000;
  ehdr.e_shentsize = sizeof(Elf64_Shdr) + 100;
  ehdr.e_shnum = 4;
  memory_->SetMemory(0x9000, &ehdr, sizeof(ehdr));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_FALSE(elf->valid());

  info.elf = nullptr;
  info.flags = PROT_READ;
  elf.reset(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
}

TEST_F(MapInfoGetElfTest, check_device_maps) {
  MapInfo info{.start = 0x7000,
               .end = 0x8000,
               .offset = 0x1000,
               .flags = PROT_READ | MAPS_FLAGS_DEVICE_MAP,
               .name = "/dev/something"};

  // Create valid elf data in process memory for this to verify that only
  // the name is causing invalid elf data.
  Elf64_Ehdr ehdr;
  TestInitEhdr<Elf64_Ehdr>(&ehdr, ELFCLASS64, EM_X86_64);
  ehdr.e_shoff = 0x2000;
  ehdr.e_shentsize = sizeof(Elf64_Shdr) + 100;
  ehdr.e_shnum = 4;
  memory_->SetMemory(0x7000, &ehdr, sizeof(ehdr));

  std::unique_ptr<Elf> elf(info.GetElf(process_memory_, false));
  ASSERT_FALSE(elf->valid());

  // Set the name to nothing to verify that it still fails.
  info.elf = nullptr;
  info.name = "";
  elf.reset(info.GetElf(process_memory_, false));
  ASSERT_FALSE(elf->valid());

  // Change the flags and verify the elf is valid now.
  info.elf = nullptr;
  info.flags = PROT_READ;
  elf.reset(info.GetElf(process_memory_, false));
  ASSERT_TRUE(elf->valid());
}

}  // namespace unwindstack
