################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/CompressGrayPicture.c 

SRC_OBJS += \
./src/CompressGrayPicture.doj 

C_DEPS += \
./src/CompressGrayPicture.d 


# Each subdirectory must supply rules for building sources it contributes
src/CompressGrayPicture.doj: ../src/CompressGrayPicture.c
	@echo 'Building file: $<'
	@echo 'Invoking: CrossCore SHARC C/C++ Compiler'
	cc21k -c -file-attr ProjectName="CompressGrayPicture" -proc ADSP-21489 -flags-compiler --no_wrap_diagnostics -si-revision 0.2 -O -Ov100 -g -DCORE0 -DDO_CYCLE_COUNTS -D_DEBUG @includes-cbef974e54f0b7691be7b2db406f2d23.txt -structs-do-not-overlap -no-const-strings -no-multiline -warn-protos -double-size-32 -swc -gnu-style-dependencies -MD -Mo "src/CompressGrayPicture.d" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


