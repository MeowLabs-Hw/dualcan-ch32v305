################################################################################
# MRS Version: 2.4.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/can_drv.c \
../User/ch32v30x_it.c \
../User/gs_can.c \
../User/led.c \
../User/main.c \
../User/system_ch32v30x.c \
../User/usb_desc.c \
../User/usbhs_dev.c 

C_DEPS += \
./User/can_drv.d \
./User/ch32v30x_it.d \
./User/gs_can.d \
./User/led.d \
./User/main.d \
./User/system_ch32v30x.d \
./User/usb_desc.d \
./User/usbhs_dev.d 

OBJS += \
./User/can_drv.o \
./User/ch32v30x_it.o \
./User/gs_can.o \
./User/led.o \
./User/main.o \
./User/system_ch32v30x.o \
./User/usb_desc.o \
./User/usbhs_dev.o 

DIR_OBJS += \
./User/*.o \

DIR_DEPS += \
./User/*.d \

DIR_EXPANDS += \
./User/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GNU RISC-V Cross C Compiler'
	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"/home/meow/mounriver-studio-projects/CH32V305RBT6SOCKETCANINTERFACE/Debug" -I"/home/meow/mounriver-studio-projects/CH32V305RBT6SOCKETCANINTERFACE/Core" -I"/home/meow/mounriver-studio-projects/CH32V305RBT6SOCKETCANINTERFACE/User" -I"/home/meow/mounriver-studio-projects/CH32V305RBT6SOCKETCANINTERFACE/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@

