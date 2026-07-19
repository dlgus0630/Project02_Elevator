################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../BSP/Src/dev_button.c \
../BSP/Src/dev_buzzer.c \
../BSP/Src/dev_dht.c \
../BSP/Src/dev_display.c \
../BSP/Src/dev_photo.c \
../BSP/Src/dev_servo.c \
../BSP/Src/dev_stepper.c 

OBJS += \
./BSP/Src/dev_button.o \
./BSP/Src/dev_buzzer.o \
./BSP/Src/dev_dht.o \
./BSP/Src/dev_display.o \
./BSP/Src/dev_photo.o \
./BSP/Src/dev_servo.o \
./BSP/Src/dev_stepper.o 

C_DEPS += \
./BSP/Src/dev_button.d \
./BSP/Src/dev_buzzer.d \
./BSP/Src/dev_dht.d \
./BSP/Src/dev_display.d \
./BSP/Src/dev_photo.d \
./BSP/Src/dev_servo.d \
./BSP/Src/dev_stepper.d 


# Each subdirectory must supply rules for building sources it contributes
BSP/Src/%.o BSP/Src/%.su BSP/Src/%.cyclo: ../BSP/Src/%.c BSP/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../App/Inc -I../BSP/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-BSP-2f-Src

clean-BSP-2f-Src:
	-$(RM) ./BSP/Src/dev_button.cyclo ./BSP/Src/dev_button.d ./BSP/Src/dev_button.o ./BSP/Src/dev_button.su ./BSP/Src/dev_buzzer.cyclo ./BSP/Src/dev_buzzer.d ./BSP/Src/dev_buzzer.o ./BSP/Src/dev_buzzer.su ./BSP/Src/dev_dht.cyclo ./BSP/Src/dev_dht.d ./BSP/Src/dev_dht.o ./BSP/Src/dev_dht.su ./BSP/Src/dev_display.cyclo ./BSP/Src/dev_display.d ./BSP/Src/dev_display.o ./BSP/Src/dev_display.su ./BSP/Src/dev_photo.cyclo ./BSP/Src/dev_photo.d ./BSP/Src/dev_photo.o ./BSP/Src/dev_photo.su ./BSP/Src/dev_servo.cyclo ./BSP/Src/dev_servo.d ./BSP/Src/dev_servo.o ./BSP/Src/dev_servo.su ./BSP/Src/dev_stepper.cyclo ./BSP/Src/dev_stepper.d ./BSP/Src/dev_stepper.o ./BSP/Src/dev_stepper.su

.PHONY: clean-BSP-2f-Src

