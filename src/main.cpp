/*
  Wind - NMEA Wind Instrument
  Copyright (c) 2018 Tom K
  SensESP adaptation (c) 2023 Tobias R

  MIT License

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

#include "Version.h"
#include "Arduino.h"
#include "sensesp.h"
#include "sensesp_app_builder.h"
#include "ui_configurables.h"

using namespace sensesp;

#define windSpeedPin 12
#define windDirPin 14

const unsigned long DEBOUNCE = 10000ul;      // Minimum switch time in microseconds
const unsigned long TIMEOUT = 1500000ul;       // Maximum time allowed between speed pulses in microseconds

// Speed is actually stored as cm/s (or "m/s * 100"). Deviations below should match these units.
const int BAND_0 =  5 * 100;
const int BAND_1 =  40 * 100;

const int SPEED_DEV_LIMIT_0 =  5 * 100;     // Deviation from last measurement to be valid. Band_0: 0 to 5 m/s
const int SPEED_DEV_LIMIT_1 = 10 * 100;     // Deviation from last measurement to be valid. Band_1: 5 to 40 m/s
const int SPEED_DEV_LIMIT_2 = 30 * 100;     // Deviation from last measurement to be valid. Band_2: 40+ m/s

// Should be larger limits as lower speed, as the direction can change more per speed update
const int DIR_DEV_LIMIT_0 = 25;     // Deviation from last measurement to be valid. Band_0: 0 to 5 m/s
const int DIR_DEV_LIMIT_1 = 18;     // Deviation from last measurement to be valid. Band_1: 5 to 40 m/s
const int DIR_DEV_LIMIT_2 = 10;     // Deviation from last measurement to be valid. Band_2: 40+ m/s

volatile unsigned long speedPulse = 0ul;    // Time capture of speed pulse
volatile unsigned long dirPulse = 0ul;      // Time capture of direction pulse
volatile unsigned long speedTime = 0ul;     // Time between speed pulses (microseconds)
volatile unsigned long directionTime = 0ul; // Time between direction pulses (microseconds)

volatile int speedOut = 0;    // Wind speed output in cm/s (divide by 100 for m/s)
volatile int dirOut = 0;      // Direction output in degrees
volatile boolean ignoreNextReading = false;
volatile long rps = 0l;

SKMetadata* speed_meta;
SKMetadata* dir_meta;
SKOutputFloat* speed_output;
SKOutputFloat* dir_output;
FloatConfig *filter_gain;
IntConfig *dir_offset;
CheckboxConfig *debug;

// initial function declarations
void IRAM_ATTR readWindSpeed();
void IRAM_ATTR readWindDir();
boolean checkSpeedDev(long cmps, int dev);
boolean checkDirDev(long cmps, int dev);
void calcWindSpeedAndDir();
void printDebug();

ReactESP app;

void setup()
{
    #ifndef SERIAL_DEBUG_DISABLED
      SetupSerialDebug(115200);
    #endif

    Serial.printf("SensESP-PeetBrosWind version v%s, built %s\n",VERSION,BUILD_TIMESTAMP);

    SensESPAppBuilder builder;
    sensesp_app = (&builder)
                  ->set_hostname("SensESP-PeetBrosWind")
                  // Optionally, hard-code the WiFi and Signal K server
                  // settings. This is normally not needed.
                  //->set_wifi("My WiFi SSID", "my_wifi_password")
                  //->set_sk_server("192.168.10.3", 80)
                  ->enable_ota("mypassword")
                  ->enable_system_info_sensors()
                  ->get_app();

    debug = new CheckboxConfig(false, "debug", "/Settings/Debug Output on Serial", "Enable debug output to USB Serial (115200 8N1)", 700);

    const char* speed_path = "environment.wind.speedApparent";
    const char* dir_path = "environment.wind.angleApparent";

    speed_meta = new SKMetadata("m/s", "Apparent Wind Speed", "", "AWS", 1.0);
    dir_meta = new SKMetadata("rad", "Apparent Wind Angle", "", "AWA", 1.0);

    speed_output = new SKOutputFloat(speed_path, speed_meta);
    dir_output = new SKOutputFloat(dir_path, dir_meta);

    filter_gain = new FloatConfig(0.25, "/Settings/Filter Gain", "Filter gain on direction output filter. Range: 0.0 to 1.0, where 1.0 means no filtering. A smaller number increases the filtering.", 600);
    dir_offset = new IntConfig(0, "/Settings/Direction Offset", "Offset (in degrees) between device-north and direction in which boat is pointing", 500);

    pinMode(windSpeedPin, INPUT_PULLUP);
    app.onInterrupt(windSpeedPin, FALLING, []() {readWindSpeed();});

    pinMode(windDirPin, INPUT_PULLUP);
    app.onInterrupt(windDirPin, FALLING, []() {readWindDir();});

    app.onRepeat(200, []() {calcWindSpeedAndDir();});
    app.onRepeat(200, []() {if (debug->get_value()) {printDebug();}});

    sensesp_app->start();
}

void IRAM_ATTR readWindSpeed()
{
    // Despite the interrupt being set to FALLING edge, double check the pin is now LOW
    if (((micros() - speedPulse) > DEBOUNCE) && (digitalRead(windSpeedPin) == LOW))
    {
        // Work out time difference between last pulse and now
        speedTime = micros() - speedPulse;
        // Direction pulse should have occured after the last speed pulse
        if (dirPulse - speedPulse >= 0) directionTime = dirPulse - speedPulse;

        speedPulse = micros();    // Capture time of the new speed pulse
    }
}

void IRAM_ATTR readWindDir()
{
    if (((micros() - dirPulse) > DEBOUNCE) && (digitalRead(windDirPin) == LOW))
    {
      dirPulse = micros();        // Capture time of direction pulse
    }
}

boolean checkSpeedDev(long cmps, int dev)
{
    if (cmps < BAND_0)
    {
        if (abs(dev) < SPEED_DEV_LIMIT_0) return true;
    }
    else if (cmps < BAND_1)
    {
        if (abs(dev) < SPEED_DEV_LIMIT_1) return true;
    }
    else
    {
        if (abs(dev) < SPEED_DEV_LIMIT_2) return true;
    }
    return false;
}

boolean checkDirDev(long cmps, int dev)
{
    if (cmps < BAND_0)
    {
        if ((abs(dev) < DIR_DEV_LIMIT_0) || (abs(dev) > 360 - DIR_DEV_LIMIT_0)) return true;
    }
    else if (cmps < BAND_1)
    {
        if ((abs(dev) < DIR_DEV_LIMIT_1) || (abs(dev) > 360 - DIR_DEV_LIMIT_1)) return true;
    }
    else
    {
        if ((abs(dev) < DIR_DEV_LIMIT_2) || (abs(dev) > 360 - DIR_DEV_LIMIT_2)) return true;
    }
    return false;
}

void calcWindSpeedAndDir()
{
    unsigned long dirPulse_, speedPulse_;
    unsigned long speedTime_;
    unsigned long directionTime_;
    long windDirection = 0l, cmps = 0l;

    static int prevSpeed = 0;
    static int prevDir = 0;
    int dev = 0;

    // Get snapshot of data into local variables. Note: an interrupt could trigger here
    noInterrupts();
    dirPulse_ = dirPulse;
    speedPulse_ = speedPulse;
    speedTime_ = speedTime;
    directionTime_ = directionTime;
    interrupts();

    // Make speed zero, if the pulse delay is too long
    if (micros() - speedPulse_ > TIMEOUT) speedTime_ = 0ul;

    // The following converts revolutions per 100 seconds (rps) to cm/s 
    // (cm/s simply for precision and speed, divide by 100 later to get m/s)
    // This calculation follows the Peet Bros. piecemeal calibration data
    if (speedTime_ > 0)
    {
        rps = 100000000/speedTime_;                  //revolutions per 100s

        if (rps < 323)
        {
          cmps = (rps * rps * -11)/22369 + (293 * rps)/223 - 12;
        }
        else if (rps < 5436)
        {
          cmps = (rps * rps / 2)/22369 + (220 * rps)/223 + 96;
        }
        else
        {
          cmps = (rps * rps * 11)/22369 - (957 * rps)/223 + 28664;
        }

        if (cmps < 0l) cmps = 0l;  // Remove the possibility of negative speed
        // Find deviation from previous value
        dev = (int)cmps - prevSpeed;

        // Only update output if in deviation limit
        if (checkSpeedDev(cmps, dev))
        {
          speedOut = cmps;

          // If speed data is ok, then continue with direction data
          if (directionTime_ > speedTime_)
          {
              windDirection = 999;    // For debugging only (not output to speed)
          }
          else
          {
            // Calculate direction from captured pulse times
            windDirection = (((directionTime_ * 360) / speedTime_) - dir_offset->get_value()) % 360;
            // resulting direction was reversed, rotating the wind vane clockwise gave counterclockwise readings, this corrects it:
            windDirection = 360 - windDirection;

            // Find deviation from previous value
            dev = (int)windDirection - prevDir;

            // Check deviation is in range
            if (checkDirDev(cmps, dev))
            {
              int delta = ((int)windDirection - dirOut);
              if (delta < -180)
              {
                delta = delta + 360;    // Take the shortest path when filtering
              }
              else if (delta > +180)
              {
                delta = delta - 360;
              }
              // Perform filtering to smooth the direction output
              dirOut = (dirOut + (int)(round(filter_gain->get_value() * delta))) % 360;
              if (dirOut < 0) dirOut = dirOut + 360;
            }
            prevDir = windDirection;
          }
        }
        else
        {
          ignoreNextReading = true;
        }

        prevSpeed = cmps;    // Update, even if outside deviation limit, cause it might be valid!?
    }
    else
    {
        speedOut = 0;
        prevSpeed = 0;
    }

    speed_output->set_input((speedOut/100));
    dir_output->set_input((dirOut*0.0174533));
}

void printDebug()
{
//  Serial.printf("millis: %lu,", millis()); // -- breaks Arduino Serial Plotter output
  Serial.printf("f_g: %f,", filter_gain->get_value());
  Serial.printf("d_o: %i,", dir_offset->get_value());
  Serial.printf("dir_raw: %i,", dirOut);
  Serial.printf("dir_adj: %f,", (dirOut*0.0174533));
  Serial.printf("spd_raw: %i,", speedOut);
  Serial.printf("spd_adj: %f,", (speedOut/100.0));
  Serial.printf("rps: %d\n", rps);
}

void loop()
{
 app.tick();
}
