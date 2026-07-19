################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Src/app_elevator_fsm.c \
../App/Src/app_rs485_ctrl.c 

OBJS += \
./App/Src/app_elevator_fsm.o \
./App/Src/app_rs485_ctrl.o 

C_DEPS += \
./App/Src/app_elevator_fsm.d \
./App/Src/app_rs485_ctrl.d 


# Each subdirectory must supply rules for building sources it contributes
App/Src/%.o App/Src/%.su App/Src/%.cyclo: ../App/Src/%.c App/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../App/Inc -I../BSP/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Src

clean-App-2f-Src:
	-$(RM) ./App/Src/app_elevator_fsm.cyclo ./App/Src/app_elevator_fsm.d ./App/Src/app_elevator_fsm.o ./App/Src/app_elevator_fsm.su ./App/Src/app_rs485_ctrl.cyclo ./App/Src/app_rs485_ctrl.d ./App/Src/app_rs485_ctrl.o ./App/Src/app_rs485_ctrl.su

.PHONY: clean-App-2f-Src

