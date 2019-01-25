/*


MIT License

Copyright (c) 2019 Tadeusz Puźniakowski

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


*/

#include <chrono>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <map>

// RASPBERRY PI GPIO - docs from http://www.pieter-jan.com/node/15

#include <stdio.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>


/**


Select Pi1 or Pi3


**/



// #define BCM2708_PERI_BASE       0x20000000   // raspi 1 //
#define BCM2708_PERI_BASE 0x3F000000 // raspi 3 //

#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000)









#define BLOCK_SIZE (4 * 1024)

#define INP_GPIO(g) *(gpio.addr + ((g) / 10)) &= ~(7 << (((g) % 10) * 3))
#define OUT_GPIO(g) *(gpio.addr + ((g) / 10)) |= (1 << (((g) % 10) * 3))
#define SET_GPIO_ALT(g, a)         \
    *(gpio.addr + (((g) / 10))) |= \
        (((a) <= 3 ? (a) + 4 : (a) == 4 ? 3 : 2) << (((g) % 10) * 3))

#define GPIO_SET \
    *(gpio.addr + 7) // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR \
    *(gpio.addr + 10) // clears bits which are 1 ignores bits which are 0

#define GPIO_READ(g) *(gpio.addr + 13) &= (1 << (g))

#define GET_GPIO(g) (*(gpio.addr + 13) & (1 << g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio.addr + 37)     // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio.addr + 38) // Pull up/pull down clock

// IO Acces
struct bcm2835_peripheral {
    unsigned long addr_p;
    int mem_fd;
    void* map;
    volatile unsigned int* addr;
};

struct bcm2835_peripheral gpio;

void enable_stepper(bool isen, int step, int dir, int en)
{
        if (isen) {
            GPIO_CLR = 1 << en;
        } else {
            GPIO_SET = 1 << en;
        }
}

void init(int step, int dir, int en)
{
    using namespace std::chrono_literals;
    // setup GPIO memory access
    gpio = {GPIO_BASE, 0, 0, 0};
    // Open /dev/mem
    if ((gpio.mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
        throw std::runtime_error(
            "Failed to open /dev/mem, try checking permissions.\n");
    }

    gpio.map = mmap(
        NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
        gpio.mem_fd, // File descriptor to physical memory virtual file '/dev/mem'
        gpio.addr_p  // Address in physical map that we want this memory block to
                     // expose
    );

    if (gpio.map == MAP_FAILED) {
        throw std::runtime_error("map_peripheral failed");
    }

    gpio.addr = (volatile unsigned int*)gpio.map;

    // enable steppers
    INP_GPIO(step);
    OUT_GPIO(step);
    INP_GPIO(dir);
    OUT_GPIO(dir);
    INP_GPIO(en);
    OUT_GPIO(en);

    enable_stepper(false,step, dir, en);
}


void shutdown()
{
    munmap(gpio.map, BLOCK_SIZE);
    close(gpio.mem_fd);
}


void do_step(int step_intention,int step, int dir, int en)
{
    unsigned int step_clear = (1 << step);
    // step direction
    int dir_value = (step_intention > 0)?1:0; // 0 or 1
    int step_value = (step_intention == 0)?0:1;

    unsigned int dir_set =
        (dir_value << dir);
    unsigned int dir_clear = ((1 - dir_value) << dir);
    // shoud do step?
    unsigned int step_set =
        (step_value << step);

    // first set directions
    GPIO_SET = dir_set;
    GPIO_CLR = dir_clear;
    {
        volatile int delayloop = 50;
        while (delayloop--)
            ;
    }
    // set step to do
    GPIO_SET = step_set;
    {
        volatile int delayloop = 100;
        while (delayloop--)
            ;
    }
    // clear all step pins
    GPIO_CLR = step_clear;
    {
        volatile int delayloop = 20;
        while (delayloop--)
            ;
    }
}


int main(int argc, char **argv){
    std::vector<std::string> args(argv+1, argv+argc);
    std::map<std::string,int> cfg = {
//        {"dir", 27},
//        {"en",10},
//        {"step",22},

        {"dir", 4},
        {"en",10},
        {"step",17},

//        {"dir", 9},
//        {"en",10},
//        {"step",5},


        {"distance",2000},
        {"delay", 5000}
    };
    std::string key = "";
    for (auto e: args) {
        if (cfg.count(e)) {
            key = e;
        } else if (cfg.count(key)) {
            cfg[key] = std::stod(e);
        }
    }
    init(cfg["step"],cfg["dir"],cfg["en"]);
    enable_stepper(true,cfg["step"],cfg["dir"],cfg["en"]);
    auto prevTime = std::chrono::steady_clock::now();
    for (; cfg["distance"]; cfg["distance"] -= ((cfg["distance"]>0)?1:-1)) {
//        std::cout << "distance: " << cfg["distance"] << std::endl;
        do_step(cfg["distance"],cfg["step"],cfg["dir"],cfg["en"]);
        prevTime = prevTime + std::chrono::microseconds((int)(cfg["delay"]));
        std::this_thread::sleep_until(prevTime);
    }
    enable_stepper(false,cfg["step"],cfg["dir"],cfg["en"]);
    return 0;
}