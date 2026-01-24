################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../system/sru/sru_config.c 

SRC_OBJS += \
./system/sru/sru_config.doj 

C_DEPS += \
./system/sru/sru_config.d 


# Each subdirectory must supply rules for building sources it contributes
system/sru/sru_config.doj: ../system/sru/sru_config.c
	@echo 'Building file: $<'
	@echo 'Invoking: CrossCore SHARC C/C++ Compiler'
	cc21k -c -file-attr ProjectName="CompressRGBPicture" -proc ADSP-21489 -flags-compiler --no_wrap_diagnostics -si-revision 0.2 -O -Ov100 -g -DCORE0 -DDO_CYCLE_COUNTS -D_DEBUG @includes-5b1fe35b420cad9608e49cc51383f19c.txt -structs-do-not-overlap -no-const-strings -no-multiline -warn-protos -double-size-32 -swc -linear-simd -loop-simd -gnu-style-dependencies -MD -Mo "system/sru/sru_config.d" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


