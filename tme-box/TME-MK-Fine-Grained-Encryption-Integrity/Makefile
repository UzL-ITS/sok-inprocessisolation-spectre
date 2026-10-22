ROOT:=$(CURDIR)
MBUILDPATH:=$(abspath mbuild)
NUM_CORES?=$(shell nproc)
NUM_KEYS?=63
PINNED_CORE?=23

.NOTPARALLEL:

GENV:=PATH=$(shell readlink -f ../depot_tools):$(PATH) DEPOT_TOOLS_UPDATE=0

# ------------------------------------------------------------------------------
GLIBC_SOURCE_DIR:=$(ROOT)/../c3-glibc
GLIBC_INSTALL_DIR:=$(ROOT)/../glibc_install
GLIBC_BUILD_DIR:=$(ROOT)/../glibc_build
LDFLAGS = -nostdlib -nostartfiles -static -L$(GLIBC_INSTALL_DIR)/lib/
STARTFILES = $(GLIBC_BUILD_DIR)/csu/crt1.o $(GLIBC_BUILD_DIR)/csu/crti.o `gcc --print-file-name=crtbegin.o`
ENDFILES = `gcc --print-file-name=crtend.o` $(GLIBC_BUILD_DIR)/csu/crtn.o
LIBGROUP = -Wl,--start-group $(GLIBC_BUILD_DIR)/libc.a -lgcc -lgcc_eh -Wl,--end-group
LDFLAGS_CUSTOM_GLIBC_DYNAMIC = -Wl,--rpath=$(GLIBC_INSTALL_DIR)/lib -Wl,--dynamic-linker=$(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2
# ------------------------------------------------------------------------------
CFLAGS_NOAVX=-mavx -mavx2 -mavx512f -mavx512pf -mavx512er -mavx512cd -mavx512vl -mavx512bw -mavx512dq -mavx512ifma -mavx512vbmi
# ------------------------------------------------------------------------------
# How to launch the patchelf'd test binaries: pinned to PINNED_CORE, with the
# custom glibc on the library search path and its AVX/SSE string routines
# disabled.
#
# `taskset ... env` MUST come *before* the environment assignments. taskset is
# an ordinary system binary and keeps the system interpreter
# (/lib64/ld-linux-x86-64.so.2), so putting LD_LIBRARY_PATH into taskset's own
# environment makes the system ld.so bind libc.so.6 to our older custom
# libc-2.30.so instead. ld.so and libc.so.6 are a version-locked pair (they
# share internal state through GLIBC_PRIVATE symbols such as _dl_argv), and the
# mismatched pair segfaults during startup, before taskset ever execs the test
# binary. Wrapping the assignments in `env` keeps them out of taskset's
# environment; the test binaries themselves are fine because patchelf gave them
# the matching interpreter from GLIBC_INSTALL_DIR.
#
# LD_LIBRARY_PATH is still required even though patchelf also sets an rpath:
# patchelf writes DT_RUNPATH, which does not apply to the dependencies of our
# dependencies (libc++.so needs libpthread.so.0 from the custom glibc).
#
# The *_plain targets deliberately do not use this: they link against the system
# glibc, so they only need `taskset -c $(PINNED_CORE)`.
RUN_PINNED = taskset -c $(PINNED_CORE) env \
             GLIBC_TUNABLES=glibc.cpu.hwcaps=-AVX2_Usable,-AVX_Fast_Unaligned_Load,-AVX,-SSE2 \
             LD_LIBRARY_PATH=$$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:$$LD_LIBRARY_PATH
# ------------------------------------------------------------------------------
default:
	false
# ------------------------------------------------------------------------------
.PHONY: package_dependencies
package_dependencies:
	echo "pass" | sudo -S apt install git fakeroot build-essential xz-utils libssl-dev bc flex libelf-dev bison libkeyutils-dev keyutils dwarves cmake linux-tools-common msr-tools cgroup-tools libelf-dev libdw-dev clang gawk pkg-config patchelf libbsd-dev autoconf python3-pip
# # ------------------------------------------------------------------------------
CFLAGS += -g -O0 -Wall -Wshadow -Wno-unused-function -Wno-discarded-qualifiers -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable -Werror=shadow -fPIC

# ------------------------------------------------------------------------------
# PartitionAlloc

PA_SO_FILENAME1:=libbase_allocator_partition_allocator_partition_alloc.so
PA_SO_FILENAME2:=libc++.so
PA_SO_PATH:=$(shell readlink -f ../partition_alloc_builder)/out/Default
PA_SO_PATH_FULL:=$(PA_SO_PATH)/$(PA_SO_FILENAME1)

$(PA_SO_PATH_FULL): partition

partition_dependencies=
ifeq ($(wildcard ../partition_alloc_builder/.*),)
partition_dependencies+=../partition_alloc_builder
else
partition_dependencies+=../partition_alloc_builder/out/Default
endif

.PHONY: partition
# partition: $(partition_dependencies) preallocate_keys_if_necessary
partition: $(partition_dependencies) install_glibc
	touch /tmp/.tmp; if [ "../partition_alloc_builder/base/allocator/partition_allocator/BUILD.gn" -nt "/tmp/.tmp" ] ; then touch ../partition_alloc_builder/base/allocator/partition_allocator/BUILD.gn; fi
	cd ../partition_alloc_builder && $(GENV) autoninja -C out/Default
../depot_tools:
	cd .. && git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
#	export PATH=$(shell readlink -f ../depot_tools):$$PATH
#	echo 'export PATH=$(shell readlink -f ../depot_tools):$$PATH' >> ~/.zshrc
#	echo 'export PATH=$(shell readlink -f ../depot_tools):$$PATH' >> ~/.bashrc
../llvm-project:
	cd .. && git clone -b release/16.x --single-branch https://github.com/llvm/llvm-project.git
../partition_alloc_builder: ../depot_tools ../llvm-project
	if [ ! -d "../partition_alloc_builder" ] ; then cd .. && git clone https://github.com/yuki3/partition_alloc_builder.git ; fi
	git -C ../partition_alloc_builder checkout f2196fb
	cd .. && $(GENV) gclient config https://github.com/yuki3/partition_alloc_builder.git
#   For some reason, the next line fails when run in Docker. Run it manually outside of Docker and let the Container continue from 'make partition_alloc_builder_patch'
	cd ../partition_alloc_builder/ && $(GENV) gclient sync
	make partition_alloc_builder_patch
.PHONY: partition_alloc_builder_patch
partition_alloc_builder_patch:
	git -C ../partition_alloc_builder checkout f2196fb416a53d7660573f74c90e6dab643db48b
	git -C ../partition_alloc_builder/buildtools checkout 0cf6a99585d596aca9cb4fb70175fe7307ef183e
	git -C ../partition_alloc_builder/buildtools/clang_format/script checkout e5337933f2951cacd3aeacd238ce4578163ca0b9
	rm -rf ../partition_alloc_builder/buildtools/third_party/libc++/trunk/* ../partition_alloc_builder/buildtools/third_party/libc++abi/trunk/*
	cp -r ../llvm-project/libcxx/*    ../partition_alloc_builder/buildtools/third_party/libc++/trunk
	cp -r ../llvm-project/libcxxabi/* ../partition_alloc_builder/buildtools/third_party/libc++abi/trunk
	true || git -C ../partition_alloc_builder/buildtools/third_party/libc++/trunk checkout 8df190fe9cdd29546a6f4ef7721db45b808c221f
	true || git -C ../partition_alloc_builder/buildtools/third_party/libc++abi/trunk checkout 8d21803b9076b16d46c32e2f10da191ee758520c
	git -C ../partition_alloc_builder/testing checkout f99dbeb6aff754c3409802f6860551d834641a7f
	git -C ../partition_alloc_builder/third_party/googletest/src checkout 9b12f749fa972d08703d8459e9bf3239617491ca
	git -C ../partition_alloc_builder/third_party/lss checkout 9719c1e1e676814c456b55f5f070eabad6709d31
	git -C ../partition_alloc_builder/build_overrides stash
	git -C ../partition_alloc_builder/build_overrides checkout 9de4031586f84ecc31cc512834b18f322cb7a34b
	git -C ../partition_alloc_builder/build checkout 7ff5783a50d3f77446c494d0bf3a809ca8d0c0a5
	git -C ../partition_alloc_builder/base/allocator/partition_allocator stash
	git -C ../partition_alloc_builder/base/allocator/partition_allocator checkout 8ef7102f9e16d0fd974cd3309f70d6150d16aa57
	git -C ../partition_alloc_builder/tools/clang stash
	git -C ../partition_alloc_builder/tools/clang checkout 86501dda633aaea3f8b4c133be4752e9e70069be
	sed -i '/--verify-version/d' ../partition_alloc_builder/build/config/compiler/BUILD.gn
	make llvmfixup
	if [ ! -f "../partition_alloc_builder/base/base_export.h" ] ; then cd ../partition_alloc_builder/base && wget https://raw.githubusercontent.com/chromium/chromium/72a4cb693b057d4e98edc15d9ffb950215e4a029/base/base_export.h ; fi
	git -C ../partition_alloc_builder/base/allocator/partition_allocator apply $(ROOT)/patches/partitionalloc_8ef7102.patch
	touch ../partition_alloc_builder/build/config/cast.gni
	touch ../partition_alloc_builder/build/config/unwind.gni
	echo "put_ref_count_in_previous_slot_default = true" >> ../partition_alloc_builder/build_overrides/partition_alloc.gni
	sed -i 's/^use_cxx17 = false/#use_cxx17 = false/' ../partition_alloc_builder/build_overrides/build.gni
	make ../partition_alloc_builder/out/Default
.PHONY: llvmfixup
llvmfixup:
	python3 ../partition_alloc_builder/tools/clang/scripts/update.py --print-revision || true
	sed -i "s/RELEASE_VERSION = '.*'/RELEASE_VERSION = '17'/" ../partition_alloc_builder/tools/clang/scripts/update.py
	sed -i "s/CLANG_SUB_REVISION = .*/CLANG_SUB_REVISION = 1/" ../partition_alloc_builder/tools/clang/scripts/update.py
	sed -i "s/llvmorg-[0-9]*-init-[0-9]*-[0-9a-zA-Z-]*/llvmorg-17-init-4759-g547e3456/" ../partition_alloc_builder/tools/clang/scripts/update.py
	sed -i "s/llvmorg-[0-9]*-init-[0-9]*-[0-9a-zA-Z-]*/llvmorg-17-init-4759-g547e3456-1/" ../partition_alloc_builder/tools/clang/scripts/build.py
	python3 ../partition_alloc_builder/tools/clang/scripts/update.py --print-revision || true
	python3 ../partition_alloc_builder/tools/clang/scripts/update.py

# gn and ninja are CIPD dependencies (see ../partition_alloc_builder/DEPS) that
# `gclient sync` is supposed to install, but that step fails inside Docker (see
# the note on the gclient sync line above). Fetch them directly via cipd if missing.
.PHONY: ../partition_alloc_builder/out/Default
../partition_alloc_builder/out/Default: #../partition_alloc_builder
	DEPOT_TOOLS_UPDATE=0 $(shell readlink -f ../depot_tools)/ensure_bootstrap
	if [ ! -x ../partition_alloc_builder/buildtools/linux64/gn ]; then \
		printf 'gn/gn/linux-amd64 latest\n' | $(GENV) cipd ensure -root ../partition_alloc_builder/buildtools/linux64 -ensure-file - ; \
	fi
	if [ ! -x ../partition_alloc_builder/third_party/ninja/ninja ]; then \
		printf 'infra/3pp/tools/ninja/linux-amd64 latest\n' | $(GENV) cipd ensure -root ../partition_alloc_builder/third_party/ninja -ensure-file - ; \
	fi
	# gclient writes build/config/gclient_args.gni from the `gclient_gn_args`
	# list in DEPS; since `gclient sync` doesn't run in Docker, generate it.
	if [ ! -f ../partition_alloc_builder/build/config/gclient_args.gni ]; then \
		printf '# Generated by Makefile (gclient sync does not run in Docker).\nbuild_with_chromium = false\n' > ../partition_alloc_builder/build/config/gclient_args.gni ; \
	fi
	# Install the linux x64 sysroot (the sysroot_x64 hook in DEPS) if missing.
	if [ ! -d ../partition_alloc_builder/build/linux/debian_bullseye_amd64-sysroot ]; then \
		python3 ../partition_alloc_builder/build/linux/sysroot_scripts/install-sysroot.py --arch=amd64 ; \
	fi
	cd ../partition_alloc_builder/ && EDITOR=true $(GENV) gn args out/Default

.PHONY: partition_clean
partition_clean: $(partition_dependencies)
	rm -rf ../partition_alloc_builder/out/Default

.PHONY: partition_patch_only
partition_patch_only:
	git -C ../partition_alloc_builder/base/allocator/partition_allocator checkout -- .
	rm -f ../partition_alloc_builder/base/allocator/partition_allocator/dot/address-space.dot
	rm -f ../partition_alloc_builder/base/allocator/partition_allocator/dot/address-space.png
	rm -f ../partition_alloc_builder/base/allocator/partition_allocator/pointers/raw_ptr_noop_impl.h
	rm -f ../partition_alloc_builder/base/allocator/partition_allocator/starscan/stack/asm/riscv64/push_registers_asm.cc
	git -C ../partition_alloc_builder/base/allocator/partition_allocator apply $(ROOT)/patches/partitionalloc_8ef7102.patch
	make ../partition_alloc_builder/out/Default

# ------------------------------------------------------------------------------
# PartitionAlloc configuration

PACFG=../partition_alloc_builder/out/Default/args.gn

.PHONY: _partition_prepare_default
_partition_prepare_default: partition_clean
	cd ../partition_alloc_builder/ && EDITOR=true $(GENV) gn args out/Default

.PHONY: _partition_prepare_common
_partition_prepare_common: partition_clean
	cd ../partition_alloc_builder/ && EDITOR=true $(GENV) gn args out/Default
	echo "tmemk_enable_debug             = false" >> $(PACFG)
	echo "tmemk_override_debug           = false" >> $(PACFG)
	echo "tmemk_override_debug_expensive_checks = false" >> $(PACFG)
	echo "tmemk_returnnull               = true"  >> $(PACFG)
	echo "tmemk_align_at_shim            = false" >> $(PACFG)

.PHONY: _partition_prepare_with_encryption_basics
_partition_prepare_with_encryption_basics: _partition_prepare_common
	echo "tmemk_starting_tags_random     = true"  >> $(PACFG)
	echo "tmemk_enable                   = true"  >> $(PACFG)
	echo "tmemk_encryption               = true"  >> $(PACFG)
	echo "tmemk_integrity                = true"  >> $(PACFG)
	echo "tmemk_padding                  = true"  >> $(PACFG)
	echo "tmemk_notagging                = false" >> $(PACFG)
	echo "tmemk_noalias                  = false" >> $(PACFG)

.PHONY: partition_prepare_stock
partition_prepare_stock: _partition_prepare_common
	echo "tmemk_enable                   = false" >> $(PACFG)
	echo "tmemk_encryption               = false" >> $(PACFG)
	echo "tmemk_integrity                = false" >> $(PACFG)
	echo "tmemk_notagging                = true"  >> $(PACFG)
	echo "tmemk_noalias                  = true"  >> $(PACFG)
	echo "tmemk_padding                  = false" >> $(PACFG)

.PHONY: partition_prepare_just_padding
partition_prepare_just_padding: _partition_prepare_common
	echo "tmemk_enable                   = false" >> $(PACFG)
	echo "tmemk_padding                  = true"  >> $(PACFG)

.PHONY: partition_prepare_twostartingtags
partition_prepare_twostartingtags: _partition_prepare_with_encryption_basics
	echo "tmemk_starting_tags            = true"  >> $(PACFG)
	echo "tmemk_starting_tags_same       = false" >> $(PACFG)
	echo "tmemk_thread_isolation         = false" >> $(PACFG)
	echo "tmemk_never_increment          = false" >> $(PACFG)
	echo "tmemk_skip_memset_when_never_increment = false" >> $(PACFG)

.PHONY: partition_prepare_twostartingtags_no_uaf
partition_prepare_twostartingtags_no_uaf: _partition_prepare_with_encryption_basics
	echo "tmemk_starting_tags            = true"  >> $(PACFG)
	echo "tmemk_starting_tags_same       = false" >> $(PACFG)
	echo "tmemk_thread_isolation         = false" >> $(PACFG)
	echo "tmemk_never_increment          = true"  >> $(PACFG)
	echo "tmemk_skip_memset_when_never_increment = true" >> $(PACFG)

.PHONY: partition_prepare_nostartingtags
partition_prepare_nostartingtags: _partition_prepare_with_encryption_basics
	echo "tmemk_starting_tags            = false" >> $(PACFG)
	echo "tmemk_starting_tags_same       = false" >> $(PACFG)
	echo "tmemk_thread_isolation         = false" >> $(PACFG)
	echo "tmemk_never_increment          = false" >> $(PACFG)
	echo "tmemk_skip_memset_when_never_increment = false" >> $(PACFG)

.PHONY: partition_prepare_samestartingtags
partition_prepare_samestartingtags: _partition_prepare_with_encryption_basics
	echo "tmemk_starting_tags            = true"  >> $(PACFG)
	echo "tmemk_starting_tags_same       = true"  >> $(PACFG)
	echo "tmemk_thread_isolation         = false" >> $(PACFG)
	echo "tmemk_never_increment          = false" >> $(PACFG)
	echo "tmemk_skip_memset_when_never_increment = false" >> $(PACFG)

.PHONY: partition_prepare_uaf_and_tripwires
partition_prepare_uaf_and_tripwires: partition_prepare_samestartingtags
	echo "tmemk_tripwires                = true"  >> $(PACFG)

.PHONY: partition_prepare_tripwires
partition_prepare_tripwires: _partition_prepare_with_encryption_basics
	echo "tmemk_starting_tags            = true"  >> $(PACFG)
	echo "tmemk_starting_tags_same       = true"  >> $(PACFG)
	echo "tmemk_thread_isolation         = false" >> $(PACFG)
	echo "tmemk_skip_memset_when_never_increment = false" >> $(PACFG)
	echo "tmemk_tripwires                = true"  >> $(PACFG)
	echo "tmemk_never_increment          = true" >> $(PACFG)

.PHONY: partition_prepare_only_quarantine
partition_prepare_only_quarantine:
	echo "tmemk_quarantining             = true"  >> $(PACFG)

.PHONY: partition_prepare_size
partition_prepare_size:
	echo "tmemk_maxtagsize               = 1048576" >> $(PACFG)


.PHONY: partition_prepare_only_keybits_1
partition_prepare_only_keybits_1:
	echo "tmemk_keybits                  = 1"     >> $(PACFG)
.PHONY: partition_prepare_only_keybits_2
partition_prepare_only_keybits_2:
	echo "tmemk_keybits                  = 2"     >> $(PACFG)
.PHONY: partition_prepare_only_keybits_3
partition_prepare_only_keybits_3:
	echo "tmemk_keybits                  = 3"     >> $(PACFG)
.PHONY: partition_prepare_only_keybits_4
partition_prepare_only_keybits_4:
	echo "tmemk_keybits                  = 4"     >> $(PACFG)
.PHONY: partition_prepare_only_keybits_5
partition_prepare_only_keybits_5:
	echo "tmemk_keybits                  = 5"     >> $(PACFG)
.PHONY: partition_prepare_only_keybits_6
partition_prepare_only_keybits_6:
	echo "tmemk_keybits                  = 6"     >> $(PACFG)
# ------------------------------------------------------------------------------
.PHONY: glibc
glibc: $(GLIBC_BUILD_DIR)/libc.so

$(GLIBC_BUILD_DIR)/Makefile: $(GLIBC_SOURCE_DIR)
	mkdir -p $(GLIBC_BUILD_DIR)
	cd $(GLIBC_BUILD_DIR); $(GLIBC_SOURCE_DIR)/configure CFLAGS="-O2 -g -fcf-protection=none" --prefix=$(GLIBC_INSTALL_DIR) --disable-nls --enable-memory-tagging --enable-tunables=yes --enable-multi-arch=no --disable-werror
$(GLIBC_BUILD_DIR)/libc.so: $(GLIBC_BUILD_DIR)/Makefile
	make -C $(GLIBC_BUILD_DIR) -j$(shell nproc)

$(GLIBC_INSTALL_DIR)/lib/libc.so: $(GLIBC_BUILD_DIR)/libc.so
	mkdir -p $(GLIBC_INSTALL_DIR)/etc
	touch $(GLIBC_INSTALL_DIR)/etc/ld.so.conf
	make -C $(GLIBC_BUILD_DIR) install

.PHONY: install_glibc
install_glibc: $(GLIBC_INSTALL_DIR)/lib/libc.so

$(ROOT)/../c3-glibc: 
	cd $(ROOT)/.. && git clone https://github.com/IntelLabs/c3-glibc.git
	cd $(ROOT)/../c3-glibc && git checkout 86192d9
	cd $(ROOT)/../c3-glibc && git apply $(ROOT)/patches/glibc-v2.30-c3-86192d9.patch
	sed -i 's/#include "cc_globals.h"//' $(ROOT)/../c3-glibc/include/cc.h
	sed -i 's/#include "cc_globals.h"//' $(ROOT)/../c3-glibc/malloc/malloc.c
	sed -i 's/#include "cc_globals.h"//' $(ROOT)/../c3-glibc/malloc/cc_hook.c
# ------------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -rf build $(GLIBC_BUILD_DIR) $(GLIBC_INSTALL_DIR)
# ------------------------------------------------------------------------------
# Linux
../linux_5_15:
	git clone git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git ../linux_5_15
	cd ../linux_5_15 && git checkout v5.15
	cd ../linux_5_15 && git apply $(ROOT)/patches/linux_v5-15_integrity.patch

.PHONY: build_linux_initial
build_linux_initial: ../linux_5_15
	cd ../linux_5_15 && yes '' | make clean distclean
	cd ../linux_5_15 && yes '' | cp /kernel-configs/config-5.15.0-112-generic .config
	cd ../linux_5_15 && yes '' | make olddefconfig && make prepare
	cd ../linux_5_15 && scripts/config --set-str LOCALVERSION "-tmebox"
	cd ../linux_5_15 && scripts/config --disable DEBUG_INFO
	cd ../linux_5_15 && scripts/config --disable SYSTEM_TRUSTED_KEYS
	cd ../linux_5_15 && scripts/config --disable SYSTEM_REVOCATION_KEYS
	cd ../linux_5_15 && scripts/config --disable CONFIG_NET_VENDOR_NETRONOME
	cd ../linux_5_15 && scripts/config --enable CONFIG_X86_INTEL_MKTME
	cd ../linux_5_15 && scripts/config --enable CONFIG_X86_MKTME
	cd ../linux_5_15 && scripts/config --enable CONFIG_X86_MCE_INJECT
	cd ../linux_5_15 && scripts/config --enable CONFIG_ACPI_APEI_EINJ
	cd ../linux_5_15 && scripts/config --enable X86_5LEVEL
	cd ../linux_5_15 && scripts/config --set-val CONFIG_PGTABLE_LEVELS 5
#	sudo sed -i -e 's/^GRUB_TIMEOUT_STYLE=.*/GRUB_TIMEOUT_STYLE=menu/' -e 's/GRUB_TIMEOUT=.*/GRUB_TIMEOUT=10/' /etc/default/grub
#	sudo sed -i '/GRUB_SAVEDEFAULT=/d' /etc/default/grub
#	sudo sed -i -e 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved\nGRUB_SAVEDEFAULT=true/' /etc/default/grub
#	sudo sed -i 's/^MODULES=most$$/MODULES=dep/' /etc/initramfs-tools/initramfs.conf
	make build_linux NUM_CORES=$(NUM_CORES)
	make -C ../linux_5_15/tools/perf
.PHONY: build_linux
build_linux: ../linux_5_15
	cd ../linux_5_15 && yes '' | make -j$(NUM_CORES) > /dev/null
	cd ../linux_5_15 && yes '' | make -j$(NUM_CORES) modules
	cd ../linux_5_15 && yes '' | make -j$(NUM_CORES) bindeb-pkg
#	cd ../linux_5_15 && sudo make INSTALL_MOD_STRIP=1 -j$(NUM_CORES) modules_install
#	cd ../linux_5_15 && sudo make install
#	make grub
.PHONY: grub
grub:
#	sudo grub-set-default "Ubuntu, with Linux 5.15.0+"
#	sudo update-grub && sudo grub-mkconfig -o /boot/efi/EFI/ubuntu/grub.cfg
#	sudo cp /boot/grub/grubenv /boot/efi/EFI/ubuntu/grubenv
# ------------------------------------------------------------------------------
# Key allocation
.PHONY: preallocate_keys_if_necessary
preallocate_keys_if_necessary: 
	keyctl list @u | grep '$(NUM_KEYS) keys in keyring' || make preallocate_keys
	-wc -l /tmp/tmemk_keys | grep $(NUM_KEYS) || make preallocate_keys
.PHONY: preallocate_keys
preallocate_keys: deallocate_keys
	echo -n "" > /tmp/tmemk_keys
	-for i in $$(seq 1 $(NUM_KEYS)) ; do keyctl add mktme key$${i} "algorithm=aes-xts-128-i type=cpu" @u 2>/dev/null >> /tmp/tmemk_keys ; done
	-keyctl list @u | grep '$(NUM_KEYS) keys in keyring'
	-wc -l /tmp/tmemk_keys | grep $(NUM_KEYS)
.PHONY: deallocate_keys
deallocate_keys:
	keyctl clear @u
	keyctl purge mktme >/dev/null
	echo "pass" | sudo -S keyctl purge mktme >/dev/null
# ------------------------------------------------------------------------------
# program to test our PartitionAlloc
.PHONY: prepare_pa_for_testing
prepare_pa_for_testing: ../partition_alloc_builder/out/Default/current_config_is_test
#.PHONY: ../partition_alloc_builder/out/Default/current_config_is_test
../partition_alloc_builder/out/Default/current_config_is_test:
	make partition_prepare_nostartingtags partition_prepare_only_keybits_4 partition_prepare_size
	touch ../partition_alloc_builder/out/Default/current_config_is_test
	make partition
.PHONY: test
test: test.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-o $@ \
		test.c 
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_test
run_test: test
	$(RUN_PINNED) ./test \
		&& echo "[FAIL] unexpectedly, the test program did not crash" \
		|| echo "[OK] as expected, the program crashed"
# ------------------------------------------------------------------------------
# program to test Spectre PHT plain
.PHONY: pht_plain
pht_plain: spectre_pht.c cacheutils.h
	gcc \
		$(CFLAGS_NOAVX) \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
.PHONY: run_pht_plain
run_pht_plain: pht_plain
	echo "################################################################"
	echo "###   PHT Plain   ##############################################"
	taskset -c $(PINNED_CORE) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre PHT TME-Box Scenario 1
.PHONY: pht_protected_scenario1
pht_protected_scenario1: spectre_pht.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
			-D SCENARIO1 \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_pht_protected_scenario1
run_pht_protected_scenario1: pht_protected_scenario1
	echo "################################################################"
	echo "###   PHT Scenario 1   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre PHT TME-Box Scenario 2
.PHONY: pht_protected_scenario2
pht_protected_scenario2: spectre_pht.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_pht_protected_scenario2
run_pht_protected_scenario2: pht_protected_scenario2
	echo "################################################################"
	echo "###   PHT Scenario 2   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre PHT TME-Box Scenario 2 + Serialized Domain Switch
.PHONY: pht_protected_scenario2_serial
pht_protected_scenario2_serial: spectre_pht.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D SERIALIZE_SW \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_pht_protected_scenario2_serial
run_pht_protected_scenario2_serial: pht_protected_scenario2_serial
	echo "################################################################"
	echo "###   PHT Scenario 2 + Serialized Domain Switch   ##############"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre PHT TME-Box Scenario 3
.PHONY: pht_protected_scenario3
pht_protected_scenario3: spectre_pht.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_pht_protected_scenario3
run_pht_protected_scenario3: pht_protected_scenario3
	echo "################################################################"
	echo "###   PHT Scenario 3   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre PHT TME-Box Scenario 3
.PHONY: pht_protected_scenario3_exec
pht_protected_scenario3_exec: spectre_pht.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-o $@ \
		spectre_pht.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_pht_protected_scenario3_exec
run_pht_protected_scenario3_exec: pht_protected_scenario3_exec
	echo "################################################################"
	echo "###   PHT Scenario 3 + Execution Prevention   ##################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB plain
.PHONY: btb_plain
btb_plain: spectre_btb.c cacheutils.h
	gcc $(CFLAGS_NOAVX) -o $@ spectre_btb.c -lkeyutils -lm
.PHONY: run_btb_plain
run_btb_plain: btb_plain
	echo "################################################################"
	echo "###   BTB Plain   ##############################################"
	taskset -c $(PINNED_CORE) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB TME-Box Scenario 1
.PHONY: btb_protected_scenario1
btb_protected_scenario1: spectre_btb.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO1 \
		-o $@ \
		spectre_btb.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_btb_protected_scenario1
run_btb_protected_scenario1: btb_protected_scenario1
	echo "################################################################"
	echo "###   BTB Scenario 1   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB TME-Box Scenario 2
.PHONY: btb_protected_scenario2
btb_protected_scenario2: spectre_btb.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-o $@ \
		spectre_btb.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_btb_protected_scenario2
run_btb_protected_scenario2: btb_protected_scenario2
	echo "################################################################"
	echo "###   BTB Scenario 2   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB TME-Box Scenario 2 + Serialized Domain Switch
.PHONY: btb_protected_scenario2_serial
btb_protected_scenario2_serial: spectre_btb.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D SERIALIZE_SW \
		-o $@ \
		spectre_btb.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_btb_protected_scenario2_serial
run_btb_protected_scenario2_serial: btb_protected_scenario2_serial
	echo "################################################################"
	echo "###   BTB Scenario 2 + Serialized Domain Switch   ##############"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB TME-Box Scenario 3
.PHONY: btb_protected_scenario3
btb_protected_scenario3: spectre_btb.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-o $@ \
		spectre_btb.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_btb_protected_scenario3
run_btb_protected_scenario3: btb_protected_scenario3
	echo "################################################################"
	echo "###   BTB Scenario 3   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre BTB TME-Box Victim Access with Execution Prevention
.PHONY: btb_protected_scenario3_exec
btb_protected_scenario3_exec: spectre_btb.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-o $@ \
		spectre_btb.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_btb_protected_scenario3_exec
run_btb_protected_scenario3_exec: btb_protected_scenario3_exec
	echo "################################################################"
	echo "###   BTB Scenario 3 + Execution Prevention   ##################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB plain
.PHONY: rsb_plain_asm
rsb_plain_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D RETURN \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_plain
rsb_plain: rsb_plain_asm spectre_rsb.c
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-D RETURN \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
.PHONY: run_rsb_plain
run_rsb_plain: rsb_plain
	echo "################################################################"
	echo "###   RSB Plain   ##############################################"
	taskset -c $(PINNED_CORE) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 1
.PHONY: rsb_protected_scenario1_asm
rsb_protected_scenario1_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO1 \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario1
rsb_protected_scenario1: spectre_rsb.c rsb_protected_scenario1_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO1 \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario1
run_rsb_protected_scenario1: rsb_protected_scenario1
	echo "################################################################"
	echo "###   RSB Scenario 1   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 1
.PHONY: rsb_protected_scenario1_ret_asm
rsb_protected_scenario1_ret_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO1 \
		-D RETURN \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario1
rsb_protected_scenario1_ret: spectre_rsb.c rsb_protected_scenario1_ret_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO1 \
		-D RETURN \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario1_ret
run_rsb_protected_scenario1_ret: rsb_protected_scenario1_ret
	echo "################################################################"
	echo "###   RSB Scenario 1 + Return   ################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 2
.PHONY: rsb_protected_scenario2_asm
rsb_protected_scenario2_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO2 \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario2
rsb_protected_scenario2: spectre_rsb.c rsb_protected_scenario2_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario2
run_rsb_protected_scenario2: rsb_protected_scenario2
	echo "################################################################"
	echo "###   RSB Scenario 2   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 2 with Serialized Domain Switch
.PHONY: rsb_protected_scenario2_serial_asm
rsb_protected_scenario2_serial_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO2 \
		-D SERIALIZE_SW \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario2_serial
rsb_protected_scenario2_serial: spectre_rsb.c rsb_protected_scenario2_serial_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D SERIALIZE_SW \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario2_serial
run_rsb_protected_scenario2_serial: rsb_protected_scenario2_serial
	echo "################################################################"
	echo "###   RSB Scenario 2 + Serialized Domain Switch   ##############"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 2
.PHONY: rsb_protected_scenario2_ret_asm
rsb_protected_scenario2_ret_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO2 \
		-D RETURN \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario2_ret
rsb_protected_scenario2_ret: spectre_rsb.c rsb_protected_scenario2_ret_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D RETURN \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario2_ret
run_rsb_protected_scenario2_ret: rsb_protected_scenario2_ret
	echo "################################################################"
	echo "###   RSB Scenario 2 + Return   ################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 2
.PHONY: rsb_protected_scenario2_ret_serial_asm
rsb_protected_scenario2_ret_serial_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO2 \
		-D RETURN \
		-D SERIALIZE_SW \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario2_ret_serial
rsb_protected_scenario2_ret_serial: spectre_rsb.c rsb_protected_scenario2_ret_serial_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D RETURN \
		-D SERIALIZE_SW \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario2_ret_serial
run_rsb_protected_scenario2_ret_serial: rsb_protected_scenario2_ret_serial
	echo "################################################################"
	echo "###   RSB Scenario 2 + Return + Serialized Domain Switch   #####"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 3
.PHONY: rsb_protected_scenario3_asm
rsb_protected_scenario3_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO3 \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario3
rsb_protected_scenario3: spectre_rsb.c rsb_protected_scenario3_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario3
run_rsb_protected_scenario3: rsb_protected_scenario3
	echo "################################################################"
	echo "###   RSB Scenario 3   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 3 with Execution Prevention
.PHONY: rsb_protected_scenario3_exec_asm
rsb_protected_scenario3_exec_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario3_exec
rsb_protected_scenario3_exec: spectre_rsb.c rsb_protected_scenario3_exec_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario3_exec
run_rsb_protected_scenario3_exec: rsb_protected_scenario3_exec
	echo "################################################################"
	echo "###   RSB Scenario 3 + Execution Prevention   ##################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 3
.PHONY: rsb_protected_scenario3_ret_asm
rsb_protected_scenario3_ret_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO3 \
		-D RETURN \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario3_ret
rsb_protected_scenario3_ret: spectre_rsb.c rsb_protected_scenario3_ret_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D RETURN \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario3
run_rsb_protected_scenario3_ret: rsb_protected_scenario3_ret
	echo "################################################################"
	echo "###   RSB Scenario 3 + Return   ################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre RSB TME-Box Scenario 3 with Execution Prevention
.PHONY: rsb_protected_scenario3_ret_exec_asm
rsb_protected_scenario3_ret_exec_asm: spectre_rsb.asm
	nasm \
		-f elf64 \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-D RETURN \
		-o spectre_rsb.o \
		$<
.PHONY: rsb_protected_scenario3_ret_exec
rsb_protected_scenario3_ret_exec: spectre_rsb.c rsb_protected_scenario3_ret_exec_asm prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-D RETURN \
		-o $@ \
		spectre_rsb.c spectre_rsb.o
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_rsb_protected_scenario3_ret_exec
run_rsb_protected_scenario3_ret_exec: rsb_protected_scenario3_ret_exec
	echo "################################################################"
	echo "###   RSB Scenario 3 + Return + Execution Prevention   #########"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL plain
.PHONY: stl_plain
stl_plain: spectre_stl.c
	gcc $(CFLAGS_NOAVX) -o $@ spectre_stl.c -lkeyutils -lm
.PHONY: run_stl_plain
run_stl_plain: stl_plain
	echo "################################################################"
	echo "###   STL Plain   ##############################################"
	taskset -c $(PINNED_CORE) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL TME-Box SCENARIO 1
.PHONY: stl_protected_scenario1
stl_protected_scenario1: spectre_stl.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO1 \
		-o $@ \
		spectre_stl.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_stl_protected_scenario1
run_stl_protected_scenario1: stl_protected_scenario1
	echo "################################################################"
	echo "###   STL Scenario 1   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL TME-Box SCENARIO 2
.PHONY: stl_protected_scenario2
stl_protected_scenario2: spectre_stl.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-o $@ \
		spectre_stl.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_stl_protected_scenario2
run_stl_protected_scenario2: stl_protected_scenario2
	echo "################################################################"
	echo "###   STL Scenario 2   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL TME-Box Scenario 2 with Serialized Domain Switch
.PHONY: stl_protected_scenario2_serial
stl_protected_scenario2_serial: spectre_stl.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO2 \
		-D SERIALIZE_SW \
		-o $@ \
		spectre_stl.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_stl_protected_scenario2_serial
run_stl_protected_scenario2_serial: stl_protected_scenario2_serial
	echo "################################################################"
	echo "###   STL Scenario 2 + Serialized Domain Switch   ##############"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL TME-Box Scenario 3
.PHONY: stl_protected_scenario3
stl_protected_scenario3: spectre_stl.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-o $@ \
		spectre_stl.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_stl_protected_scenario3
run_stl_protected_scenario3: stl_protected_scenario3
	echo "################################################################"
	echo "###   STL Scenario 3   #########################################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# program to test Spectre STL TME-Box Scenario 3 with Execution Prevention
.PHONY: stl_protected_scenario3_exec
stl_protected_scenario3_exec: spectre_stl.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-D SCENARIO3 \
		-D EXEC_PREV \
		-o $@ \
		spectre_stl.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@
.PHONY: run_stl_protected_scenario3_exec
run_stl_protected_scenario3_exec: stl_protected_scenario3_exec
	echo "################################################################"
	echo "###   STL Scenario 3 + Execution Prevention   ##################"
	$(RUN_PINNED) ./$<
# ------------------------------------------------------------------------------
# Benchmark the primitives
.PHONY: benchmarking
benchmarking: benchmarking.c prepare_pa_for_testing
	make preallocate_keys_if_necessary NUM_KEYS=15
	gcc \
		$(CFLAGS_NOAVX) \
		-no-pie -z noexecstack \
		-L$(shell readlink -f ../partition_alloc_builder/out/Default/) \
		-o $@ \
		benchmarking.c \
		-lkeyutils -lm
	patchelf --set-interpreter $$(readlink -f $(GLIBC_INSTALL_DIR)/lib/ld-linux-x86-64.so.2) --set-rpath $$(readlink -f $(GLIBC_INSTALL_DIR)/lib)/:/usr/lib/x86_64-linux-gnu/:/lib/x86_64-linux-gnu/:/usr/lib/:/lib/x86_64-linux-gnu $@
	patchelf --add-needed $(PA_SO_PATH_FULL) --add-needed /lib/x86_64-linux-gnu/libgcc_s.so.1 ./$@

.PHONY: run_benchmarks
run_benchmarks: benchmarking
	echo "################################################################"
	echo "###   Benchmarking   ###########################################"
	$(RUN_PINNED) ./$<
