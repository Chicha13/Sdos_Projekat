################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../src/CompressRGBPicture.c 

SRC_OBJS += \
./src/CompressRGBPicture.doj 

C_DEPS += \
./src/CompressRGBPicture.d 


# Each subdirectory must supply rules for building sources it contributes
src/CompressRGBPicture.doj: ../src/CompressRGBPicture.c
	@echo 'Building file: $<'
	@echo 'Invoking: CrossCore SHARC C/C++ Compiler'
	cc21k -c -file-attr ProjectName="CompressRGBPicture" -proc ADSP-21489 -flags-compiler --no_wrap_diagnostics -si-revision 0.2 -O -Ov100 -g -DCORE0 -DDO_CYCLE_COUNTS -D_DEBUG @includes-5b1fe35b420cad9608e49cc51383f19c.txt -structs-do-not-overlap -no-const-strings -no-multiline -warn-protos -double-size-32 -swc -linear-simd -loop-simd -gnu-style-dependencies -MD -Mo "src/CompressRGBPicture.d" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


