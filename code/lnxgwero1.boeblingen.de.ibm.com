From 1089a08500830dd2c7a5edcd1011ae55fa3e501a Mon Sep 17 00:00:00 2001
From: Sidraya <sidraya.jayagond@ibm.com>
Date: Tue, 7 Feb 2023 15:33:23 +0100
Subject: [PATCH] Vector float SIMD implementation

---
 conf.sh                                       |   9 ++
 src/hotspot/cpu/s390/assembler_s390.hpp       |   3 +
 .../cpu/s390/assembler_s390.inline.hpp        |   2 +
 src/hotspot/cpu/s390/globals_s390.hpp         |   3 +
 src/hotspot/cpu/s390/registerSaver_s390.hpp   |   2 +
 src/hotspot/cpu/s390/register_s390.cpp        |   3 +
 src/hotspot/cpu/s390/register_s390.hpp        |   4 +-
 src/hotspot/cpu/s390/s390.ad                  | 144 +++++++++++++++++-
 src/hotspot/cpu/s390/sharedRuntime_s390.cpp   |   2 +-
 src/hotspot/cpu/s390/vm_version_s390.cpp      |   2 +-
 src/hotspot/cpu/s390/vmreg_s390.cpp           |   8 +
 src/hotspot/cpu/s390/vmreg_s390.hpp           |  10 ++
 src/hotspot/cpu/s390/vmreg_s390.inline.hpp    |   4 +
 13 files changed, 189 insertions(+), 7 deletions(-)
 create mode 100755 conf.sh

diff --git a/conf.sh b/conf.sh
new file mode 100755
index 00000000000..199987a7b67
--- /dev/null
+++ b/conf.sh
@@ -0,0 +1,9 @@
+bash configure \
+	--with-boot-jdk=~/jdk-19.0.1+10 \
+	--with-jtreg=../jtreg/build/images/jtreg/ \
+	--with-gtest=../googletest \
+	--with-hsdis=binutils \
+	--with-binutils-src=~/binutils-2.37/ \
+	--with-native-debug-symbols=internal \
+	--disable-precompiled-headers \
+	--with-num-cores=8
diff --git a/src/hotspot/cpu/s390/assembler_s390.hpp b/src/hotspot/cpu/s390/assembler_s390.hpp
index a0a86a707dd..09a0f1d9b02 100644
--- a/src/hotspot/cpu/s390/assembler_s390.hpp
+++ b/src/hotspot/cpu/s390/assembler_s390.hpp
@@ -1284,6 +1284,8 @@ class Assembler : public AbstractAssembler {
 #define VSTRC_ZOPC  (unsigned long)(0xe7L << 40 | 0x8aL << 0)   // String range compare
 #define VISTR_ZOPC  (unsigned long)(0xe7L << 40 | 0x5cL << 0)   // Isolate String
 
+#define VFADB_ZOPC   (unsigned long)(0xe7L << 40 | 0xE3L << 0)   // Find any element
+
 
 //--------------------------------
 //--  Miscellaneous Operations  --
@@ -2873,6 +2875,7 @@ class Assembler : public AbstractAssembler {
   inline void z_vistrhs(VectorRegister v1, VectorRegister v2);
   inline void z_vistrfs(VectorRegister v1, VectorRegister v2);
 
+  inline void z_vfadb(VectorRegister v1, VectorRegister v2, VectorRegister v3, int64_t m4);   // Find any element
 
   // Floatingpoint instructions
   // ==========================
diff --git a/src/hotspot/cpu/s390/assembler_s390.inline.hpp b/src/hotspot/cpu/s390/assembler_s390.inline.hpp
index 2eb6cfb812c..f93755b0f73 100644
--- a/src/hotspot/cpu/s390/assembler_s390.inline.hpp
+++ b/src/hotspot/cpu/s390/assembler_s390.inline.hpp
@@ -1173,6 +1173,8 @@ inline void Assembler::z_vistrbs(VectorRegister v1, VectorRegister v2)
 inline void Assembler::z_vistrhs(VectorRegister v1, VectorRegister v2)                            {z_vistr(v1, v2, VRET_HW,   VOPRC_CCSET); }
 inline void Assembler::z_vistrfs(VectorRegister v1, VectorRegister v2)                            {z_vistr(v1, v2, VRET_FW,   VOPRC_CCSET); }
 
+inline void Assembler::z_vfadb(  VectorRegister v1, VectorRegister v2, VectorRegister v3, int64_t m4)        {emit_48(VFADB_ZOPC | vreg(v1,  8) | vreg(v2, 12) | vreg(v3, 16) | vesc_mask(m4, VRET_BYTE, VRET_DW, 32)); }
+
 
 //-------------------------------
 // FLOAT INSTRUCTIONS
diff --git a/src/hotspot/cpu/s390/globals_s390.hpp b/src/hotspot/cpu/s390/globals_s390.hpp
index 24f0af92c37..5b7a93b9cba 100644
--- a/src/hotspot/cpu/s390/globals_s390.hpp
+++ b/src/hotspot/cpu/s390/globals_s390.hpp
@@ -106,6 +106,9 @@ define_pd_global(intx, InitArrayShortSize, 1*BytesPerLong);
   /* Seems to pay off with 2 pages already. */                                \
   product(size_t, MVCLEThreshold, +2*(4*K), DIAGNOSTIC,                       \
           "Threshold above which page-aligned MVCLE copy/init is used.")      \
+  /* special instructions */                                                  \
+  product(bool, SuperwordUseVSX, false,                                       \
+          "Use Z15 Vector instructions for superword optimization.")          \
                                                                               \
   product(bool, PreferLAoverADD, false, DIAGNOSTIC,                           \
           "Use LA/LAY instructions over ADD instructions (z/Architecture).")  \
diff --git a/src/hotspot/cpu/s390/registerSaver_s390.hpp b/src/hotspot/cpu/s390/registerSaver_s390.hpp
index 97883685384..8f87f3f4d92 100644
--- a/src/hotspot/cpu/s390/registerSaver_s390.hpp
+++ b/src/hotspot/cpu/s390/registerSaver_s390.hpp
@@ -65,11 +65,13 @@ class RegisterSaver {
     int_reg           = 0,
     float_reg         = 1,
     excluded_reg      = 2,  // Not saved/restored.
+    vs_reg	      = 3,
   } RegisterType;
 
   typedef enum {
     reg_size          = 8,
     half_reg_size     = reg_size / 2,
+    vs_reg_size       = 16,
   } RegisterConstants;
 
   // Remember type, number, and VMReg.
diff --git a/src/hotspot/cpu/s390/register_s390.cpp b/src/hotspot/cpu/s390/register_s390.cpp
index 853b5642470..52cf7ea76b7 100644
--- a/src/hotspot/cpu/s390/register_s390.cpp
+++ b/src/hotspot/cpu/s390/register_s390.cpp
@@ -30,6 +30,9 @@
 const int ConcreteRegisterImpl::max_gpr = RegisterImpl::number_of_registers * 2;
 const int ConcreteRegisterImpl::max_fpr = ConcreteRegisterImpl::max_gpr +
                                           FloatRegisterImpl::number_of_registers * 2;
+const int ConcreteRegisterImpl::max_vr = ConcreteRegisterImpl::max_gpr +
+					 ConcreteRegisterImpl::max_fpr +
+                                         VectorRegisterImpl::number_of_registers * 2;
 
 const char* RegisterImpl::name() const {
   const char* names[number_of_registers] = {
diff --git a/src/hotspot/cpu/s390/register_s390.hpp b/src/hotspot/cpu/s390/register_s390.hpp
index 59da743016f..f169114de5a 100644
--- a/src/hotspot/cpu/s390/register_s390.hpp
+++ b/src/hotspot/cpu/s390/register_s390.hpp
@@ -475,12 +475,14 @@ class ConcreteRegisterImpl : public AbstractRegisterImpl {
   enum {
     number_of_registers =
       (RegisterImpl::number_of_registers +
-      FloatRegisterImpl::number_of_registers)
+      FloatRegisterImpl::number_of_registers +
+      VectorRegisterImpl::number_of_registers)
       * 2 // register halves
       + 1 // condition code register
   };
   static const int max_gpr;
   static const int max_fpr;
+  static const int max_vr;
 };
 
 
diff --git a/src/hotspot/cpu/s390/s390.ad b/src/hotspot/cpu/s390/s390.ad
index 1e54b26a554..8bb3fbf8b9b 100644
--- a/src/hotspot/cpu/s390/s390.ad
+++ b/src/hotspot/cpu/s390/s390.ad
@@ -194,6 +194,38 @@ register %{
 
   reg_def Z_CR(SOC, SOC, Op_RegFlags, 0, Z_CR->as_VMReg());   // volatile
 
+reg_def Z_V0 ( NS, NS, Op_VecX, 0, Z_V0->as_VMReg());
+reg_def Z_V1 ( NS, NS, Op_VecX, 1, Z_V1->as_VMReg());
+reg_def Z_V2 ( NS, NS, Op_VecX, 2, Z_V2->as_VMReg());
+reg_def Z_V3 ( NS, NS, Op_VecX, 3, Z_V3->as_VMReg());
+reg_def Z_V4 ( NS, NS, Op_VecX, 4, Z_V4->as_VMReg());
+reg_def Z_V5 ( NS, NS, Op_VecX, 5, Z_V5->as_VMReg());
+reg_def Z_V6 ( NS, NS, Op_VecX, 6, Z_V6->as_VMReg());
+reg_def Z_V7 ( NS, NS, Op_VecX, 7, Z_V7->as_VMReg());
+reg_def Z_V8 ( NS, NS, Op_VecX, 8, Z_V8->as_VMReg());
+reg_def Z_V9 ( NS, NS, Op_VecX, 9, Z_V9->as_VMReg());
+reg_def Z_V10 ( NS, NS, Op_VecX, 10, Z_V10->as_VMReg());
+reg_def Z_V11 ( NS, NS, Op_VecX, 11, Z_V11->as_VMReg());
+reg_def Z_V12 ( NS, NS, Op_VecX, 12, Z_V12->as_VMReg());
+reg_def Z_V13 ( NS, NS, Op_VecX, 13, Z_V13->as_VMReg());
+reg_def Z_V14 ( NS, NS, Op_VecX, 14, Z_V14->as_VMReg());
+reg_def Z_V15 ( NS, NS, Op_VecX, 15, Z_V15->as_VMReg());
+reg_def Z_V16 ( NS, NS, Op_VecX, 16, Z_V16->as_VMReg());
+reg_def Z_V17 ( NS, NS, Op_VecX, 17, Z_V17->as_VMReg());
+reg_def Z_V18 ( NS, NS, Op_VecX, 18, Z_V18->as_VMReg());
+reg_def Z_V19 ( NS, NS, Op_VecX, 19, Z_V19->as_VMReg());
+reg_def Z_V20 ( NS, NS, Op_VecX, 20, Z_V20->as_VMReg());
+reg_def Z_V21 ( NS, NS, Op_VecX, 21, Z_V21->as_VMReg());
+reg_def Z_V22 ( NS, NS, Op_VecX, 22, Z_V22->as_VMReg());
+reg_def Z_V23 ( NS, NS, Op_VecX, 23, Z_V23->as_VMReg());
+reg_def Z_V24 ( NS, NS, Op_VecX, 24, Z_V24->as_VMReg());
+reg_def Z_V25 ( NS, NS, Op_VecX, 25, Z_V25->as_VMReg());
+reg_def Z_V26 ( NS, NS, Op_VecX, 26, Z_V26->as_VMReg());
+reg_def Z_V27 ( NS, NS, Op_VecX, 27, Z_V27->as_VMReg());
+reg_def Z_V28 ( NS, NS, Op_VecX, 28, Z_V28->as_VMReg());
+reg_def Z_V29 ( NS, NS, Op_VecX, 29, Z_V29->as_VMReg());
+reg_def Z_V30 ( NS, NS, Op_VecX, 30, Z_V30->as_VMReg());
+reg_def Z_V31 ( NS, NS, Op_VecX, 31, Z_V31->as_VMReg());
 
 // Specify priority of register selection within phases of register
 // allocation. Highest priority is first. A useful heuristic is to
@@ -271,6 +303,41 @@ alloc_class chunk2(
   Z_CR
 );
 
+alloc_class chunk3(
+ Z_V0,
+ Z_V1,
+ Z_V2,
+ Z_V3,
+ Z_V4,
+ Z_V5,
+ Z_V6,
+ Z_V7,
+ Z_V8,
+ Z_V9,
+ Z_V10,
+ Z_V11,
+ Z_V12,
+ Z_V13,
+ Z_V14,
+ Z_V15,
+ Z_V16,
+ Z_V17,
+ Z_V18,
+ Z_V19,
+ Z_V20,
+ Z_V21,
+ Z_V22,
+ Z_V23,
+ Z_V24,
+ Z_V25,
+ Z_V26,
+ Z_V27,
+ Z_V28,
+ Z_V29,
+ Z_V30,
+ Z_V31
+);
+
 
 //-------Architecture Description Register Classes-----------------------
 
@@ -540,6 +607,42 @@ reg_class z_dbl_reg(
 );
 reg_class z_rscratch1_dbl_reg(Z_F1,Z_F1_H);
 
+reg_class z_v_reg(
+ Z_V0,
+ Z_V1,
+ Z_V2,
+ Z_V3,
+ Z_V4,
+ Z_V5,
+ Z_V6,
+ Z_V7,
+ Z_V8,
+ Z_V9,
+ Z_V10,
+ Z_V11,
+ Z_V12,
+ Z_V13,
+ Z_V14,
+ Z_V15,
+ Z_V16,
+ Z_V17,
+ Z_V18,
+ Z_V19,
+ Z_V20,
+ Z_V21,
+ Z_V22,
+ Z_V23,
+ Z_V24,
+ Z_V25,
+ Z_V26,
+ Z_V27,
+ Z_V28,
+ Z_V29,
+ Z_V30,
+ Z_V31
+);
+
+
 %}
 
 //----------DEFINITION BLOCK---------------------------------------------------
@@ -1513,6 +1616,8 @@ const bool Matcher::match_rule_supported(int opcode) {
     case Op_PopCountL:
       // PopCount supported by H/W from z/Architecture G5 (z196) on.
       return (UsePopCountInstruction && VM_Version::has_PopCount());
+    case Op_AddVD:
+	return SuperwordUseVSX;
   }
 
   return true; // Per default match rules are supported.
@@ -1559,14 +1664,21 @@ OptoRegPair Matcher::vector_return_value(uint ideal_reg) {
 
 // Vector width in bytes.
 const int Matcher::vector_width_in_bytes(BasicType bt) {
-  assert(MaxVectorSize == 8, "");
-  return 8;
+
+    assert(MaxVectorSize == 16, "");
+    return Op_VecD;
+
+//  assert(MaxVectorSize == 8, "");
+//  return 8;
 }
 
 // Vector ideal reg.
 const uint Matcher::vector_ideal_reg(int size) {
-  assert(MaxVectorSize == 8 && size == 8, "");
-  return Op_RegL;
+    assert(MaxVectorSize == 16, "");
+    return Op_VecD;
+
+//  assert(MaxVectorSize == 8 && size == 8, "");
+//  return Op_RegL;
 }
 
 // Limits on vector size (number of elements) loaded into vector.
@@ -2465,6 +2577,14 @@ ins_attrib ins_should_rematerialize(false);
 // parsing in the ADLC because operands constitute user defined types
 // which are used in instruction definitions.
 
+operand vecX() %{
+  constraint(ALLOC_IN_RC(z_v_reg));
+  match(VecX);
+
+  format %{ %}
+  interface(REG_INTER);
+%}
+
 //----------Simple Operands----------------------------------------------------
 // Immediate Operands
 // Please note:
@@ -10691,6 +10811,22 @@ instruct reinterpret(iRegL dst) %{
   ins_pipe(pipe_class_dummy);
 %}
 
+//----------Vector Arithmetic Instructions--------------------------------------
+
+// Vector Addition Instructions
+
+instruct vadd2D_reg(vecX dst, vecX src1, vecX src2) %{
+  match(Set dst (AddVD src1 src2));
+  predicate(n->as_Vector()->length() == 2);
+  format %{ "VFADB  $dst,$src1,$src2\t// add packed2D" %}
+  size(4);
+  ins_encode %{
+    //__ z_vfadb($dst$$VectorRegister, $src1$$VectorRegister, $src2$$VectorRegister, 3);
+    __ z_vfadb(as_VectorRegister($dst$$reg),as_VectorRegister($src1$$reg), as_VectorRegister($src2$$reg),  3);
+  %}
+  ins_pipe(pipe_class_dummy);
+%}
+
 //----------POPULATION COUNT RULES--------------------------------------------
 
 // Byte reverse
diff --git a/src/hotspot/cpu/s390/sharedRuntime_s390.cpp b/src/hotspot/cpu/s390/sharedRuntime_s390.cpp
index db4d5081b05..d9970b39273 100644
--- a/src/hotspot/cpu/s390/sharedRuntime_s390.cpp
+++ b/src/hotspot/cpu/s390/sharedRuntime_s390.cpp
@@ -983,7 +983,7 @@ static void gen_special_dispatch(MacroAssembler *masm,
 // 8 bytes registers are saved by default on z/Architecture.
 bool SharedRuntime::is_wide_vector(int size) {
   // Note, MaxVectorSize == 8 on this platform.
-  assert(size <= 8, "%d bytes vectors are not supported", size);
+  assert(size <= 16, "%d bytes vectors are not supported", size);
   return size > 8;
 }
 
diff --git a/src/hotspot/cpu/s390/vm_version_s390.cpp b/src/hotspot/cpu/s390/vm_version_s390.cpp
index 759005713c8..c5399065553 100644
--- a/src/hotspot/cpu/s390/vm_version_s390.cpp
+++ b/src/hotspot/cpu/s390/vm_version_s390.cpp
@@ -97,7 +97,7 @@ void VM_Version::initialize() {
   intx cache_line_size = Dcache_lineSize(0);
 
 #ifdef COMPILER2
-  MaxVectorSize = 8;
+  MaxVectorSize = 16;
 #endif
 
   if (has_PrefetchRaw()) {
diff --git a/src/hotspot/cpu/s390/vmreg_s390.cpp b/src/hotspot/cpu/s390/vmreg_s390.cpp
index 239b68513b9..497402cd504 100644
--- a/src/hotspot/cpu/s390/vmreg_s390.cpp
+++ b/src/hotspot/cpu/s390/vmreg_s390.cpp
@@ -43,7 +43,15 @@ void VMRegImpl::set_regName() {
     regName[i++] = freg->name();
     freg = freg->successor();
   }
+
   for (; i < ConcreteRegisterImpl::number_of_registers; i ++) {
     regName[i] = "NON-GPR-XMM";
   }
+
+  VectorRegister vreg = ::as_VectorRegister(0);
+  for (; i < ConcreteRegisterImpl::max_vr;) {
+    regName[i++] = vreg->name();
+    regName[i++] = vreg->name();
+    vreg = vreg->successor();
+  }
 }
diff --git a/src/hotspot/cpu/s390/vmreg_s390.hpp b/src/hotspot/cpu/s390/vmreg_s390.hpp
index 3dd1bd9a16c..820fff3095c 100644
--- a/src/hotspot/cpu/s390/vmreg_s390.hpp
+++ b/src/hotspot/cpu/s390/vmreg_s390.hpp
@@ -35,6 +35,11 @@ inline bool is_FloatRegister() {
          value() < ConcreteRegisterImpl::max_fpr;
 }
 
+inline bool is_VectorRegister() {
+  return value() >= ConcreteRegisterImpl::max_fpr &&
+         value() < ConcreteRegisterImpl::max_vr;
+}
+
 inline Register as_Register() {
   assert(is_Register() && is_even(value()), "even-aligned GPR name");
   return ::as_Register(value() >> 1);
@@ -45,6 +50,11 @@ inline FloatRegister as_FloatRegister() {
   return ::as_FloatRegister((value() - ConcreteRegisterImpl::max_gpr) >> 1);
 }
 
+inline VectorRegister as_VectorRegister() {
+  assert(is_VectorRegister() && is_even(value()), "must be");
+  return ::as_VectorRegister((value() - ConcreteRegisterImpl::max_fpr) >> 1);
+}
+
 inline bool is_concrete() {
   assert(is_reg(), "must be");
   return is_even(value());
diff --git a/src/hotspot/cpu/s390/vmreg_s390.inline.hpp b/src/hotspot/cpu/s390/vmreg_s390.inline.hpp
index a775c8f971a..9cbe78821bb 100644
--- a/src/hotspot/cpu/s390/vmreg_s390.inline.hpp
+++ b/src/hotspot/cpu/s390/vmreg_s390.inline.hpp
@@ -41,4 +41,8 @@ inline VMReg ConditionRegisterImpl::as_VMReg() {
   return VMRegImpl::as_VMReg((encoding() << 1) + ConcreteRegisterImpl::max_fpr);
 }
 
+inline VMReg VectorRegisterImpl::as_VMReg() {
+  return VMRegImpl::as_VMReg((encoding() << 1) + ConcreteRegisterImpl::max_vr);
+}
+
 #endif // CPU_S390_VMREG_S390_INLINE_HPP
-- 
2.31.1

