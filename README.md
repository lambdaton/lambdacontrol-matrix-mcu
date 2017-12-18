# LambdaControl - RGB Button Matrix MCU

<img src="https://www.lambdaton.de/images/github/lambdacontrol-dark.jpg" alt="Picture of LambdaControl" width="420">

This repository contains the code for the RGB button matrix of my DIY MIDI controller project [LambdaControl](https://www.lambdaton.de/diy-hardware/lambda-control/). 

I designed and build LambdaControl for my upcoming live performance and decided to release all components under open source licenses, such that other artists can use my project as they like.

A complete documentation of the project can be found on my [website](https://www.lambdaton.de/diy-hardware/lambda-control/).

## Project Overview
LambdaControl is based on the [MIDIbox project](http://www.ucapps.de/), which provides a complete solution for basic tasks like reading analog and digital inputs (faders, knobs and encoders). However, the RGB button matrix of LambdaControl, which works like the matrix of a Novation Launchpad, required a complete custom development.

LambdaControl consists of the following parts: components from the MIDIbox project for the basic I/O and USB MIDI connection, separate microcontroller which controls the RGB matrix, and a MIDI Remote Script that connects the controller with Ableton Live over USB (this repository). Additionally, I created a repository for the hardware parts like the RGB button matrix PCB or the 3D printable case.

## RGB Button Matrix MCU

The RGB button matrix microcontroller unit (MCU) scans the RGB button matrix for button presses and controls the RGB leds of the different pads. Moreover, it produces a large amount of colors out of the three base colors red, blue, and green by controlling the emitted light for each color. The following sections try to explain the theory that is needed to understand the tasks of the RGB button matrix MCU.

### RGB and Colors

The following image shows the eight colors that we can produce by just combining the base colors with full or zero intensity.

<img src="https://www.lambdaton.de/images/github/base-colors.jpg" alt="Base Colors" width="420">

As you can see, all three base colors on produce in this additive color mix model white. We can produce even more colors with the RGB leds when we implement a range of intensity levels. The final system uses 4 bit for each color channel, such that 16 different intensity levels can be achieved. The following image shows the 16 levels for each of the base color.

<img src="https://www.lambdaton.de/images/github/base-color-levels.jpg" alt="Color Levels" width="420">

It is really hard to take a good picture of this with a digital camera. The human eye can really good separate the different intensity levels. We can achieve with these intensity levels in theory up to 4096 different colors. Since Ableton Live only supports 128 different clip colors, this is far enough.

### Multiplexing

Directly connecting the 60 buttons and 180 leds to the microcontroller is not an option, since this would require 60 input pins and 180 output pins. Even with using shift registers to increase the amount of available pins this is a large number. Moreover, managing this huge amount of connections would also be complex.

Therefore, the button matrix PCB, which you can find inside the hardware repository, utilizes multiplexing to decrease the number of necessary I/O pins. Multiplexing uses a matrix style connection schema to address the different components, such that they can be accessed by just using a row and column selection instead of independent pins for every input or output. So first you select the column of the led which you want to turn on and then you set the row pin so that current flows through the led. In our design this means that only one column of the matrix can be scanned or driven at the same time. Also we still use the MIDIbox shift register based input and output modules, such that we still increase our amount of available I/O pins by the help of shift registers.

However, using multiplexing increases the complexity of the firmware drastically, since we now need to frequently scan the input buttons and drive the output leds with a microcontroller. This task is really time critical, since small time deviations can result into for the human eye recognizable flickering. Hence, we use a separate MCU to do this task as consistent and fast as possible. I also tried to implement the matrix handling directly on the MIDIbox core, but the MIDIbox core is also busy with other tasks, such that there we not enough compute power to drive the matrix with so many colors without flickering.

The multiplexing gets even more complicated, since we want to use the three base colors with different intensities to produce a wide range of colors. Hence, the different time slots in which we drive the leds need also be divided into smaller chunks, such that for example we can turn the led only 50% of the time on to achieve half of the possible intensity.

Actually, the leds are driven by using interrupts. Interrupts are small routines that can be triggered by a timer and interrupt the microcontroller at its current position inside the code. After the interrupt has been finished the microcontroller continues as nothing has happened.

Pulse Width Modulation (PWM) is in our multiplexing scenario not a good option, since we would need to iterate really fast over the matrix leds to achieve a flicker-free lighting. Hence, we would need to fire a lot of interrupts. The microcontroller would be really occupied by executing the interrupts and does not have time to do other tasks like communication with the master of I2C. Therefore, we are using the in our scenario smarter Bit Angle Modulation (BAM).

### Bit Angle Modulation (BAM)

Bit Angle Modulation (BAM) allows to drastically reduce the amount of necessary interrupts to drive the led with the required intensity levels.

We can make use of BAM in our scenario, since the human eye is sluggish when actions happen really quickly, such that we can't see a difference between a led that is driven by PWM with a 50% duty-cycle or a led that is just 50% of the time off and then 50% of the time on.

BAM uses now this fact and simply adjust the times at which the interrupts are fired to decrease the overall number of necessary interrupts. We simply use in a four bit BAM four different long interrupts. Each interrupt is for example twice as long as the interrupt before. So we could end up with a 8ms, 4ms, 2ms, and 1ms long interrupts. If we want to light a led for 50% intensity, we simply just light it up in the longest interrupt and turn it off in the other interrupts. Moreover, we can easily represent this is "active in the interrupt or not" state by using four bits. Each bit represents if the led is turned on in the given interrupts. For example, 6 = 0110 represents that the led is just on in the interrupts two and three. So by using 4 bits we can achieve with this approach 16 different intensity levels.

In the actual implementation I used just on interrupt and adjusted just the next time to wait until the next interrupt is fired for code elegance and to save space. Moreover, this approach needs to work with the basic matrix multiplexing such that the actual interrupt service routine is quite complex, since the driven row and column change frequently.

Also the buttons are scanned during these interrupts for changes. Moreover, the RGB button matrix MCU debounces the inputs, such that every time only one button event is transmitted to the MIDIbox core for a button state change.

### Used Hardware

I used an Arduino Nano because of its small form factor and the nice libraries out there to drive for example shift registers or establish an I2C communication. Also the Interrupt Service Routines (ISR) of the Atmel CPU allow to easily adjust the time at which the next one is fired, which simplified the BAM implementation.

The button matrix PCB is not connected directly to the Arudino Nano instead it uses the MIDIbox input and output modules. Hence, we need to access the shift registers of these modules to access the RGB button matrix.

### I2C for Communication

The actual communication with the MIDbox core is implemented via I2C. The Arduino Nano based RGB button matrix MCU is the slave on the bus and gets the color change commands from the MIDIbox. Moreover, the MIDIbox frequently pulls the matrix MCU for changes to the buttons.

## Deploy

The source code can be easily build and deployed with the help of the [PlatformIO](http://platformio.org/) development tools. So, first install the PlatformIO bundle. I created a Makefile for the PlatformIO commands, such that you can simply build and deploy the code with the commands **make all** and **make deploy** inside the project root folder.

## License

The complete firmware is licensed under the MIT license.

## Author

2017 - LambdaTon 
  