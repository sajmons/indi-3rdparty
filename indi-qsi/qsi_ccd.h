#ifndef QSI_CCD_H
#define QSI_CCD_H

#if 0
QSI CCD
INDI Interface for Quantum Scientific Imaging CCDs
Based on FLI Indi Driver by Jasem Mutlaq.
Copyright (C) 2009 Sami Lehti (sami.lehti@helsinki.fi)

(2011-12-10) Updated by Jasem Mutlaq:
    + Driver completely rewritten to be based on INDI::CCD
      + Fixed compiler warnings.
      + Reduced message traffic.
      + Added filter name property.
      + Added snooping on telescopes.
      + Added Guider support.
      + Added Readout speed option.

      This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#endif

#include <indiccd.h>
#include <indiguiderinterface.h>
#include <indifilterinterface.h>
#include <iostream>

using namespace std;

class QSICCD : public INDI::CCD, public INDI::FilterInterface
{
public:
    QSICCD();
    virtual ~QSICCD();

    virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;
    virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
    virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;

protected:

    const char *getDefaultName() override;

    bool initProperties() override;
    bool updateProperties() override;

    bool Connect() override;
    bool Disconnect() override;

    int SetTemperature(double temperature) override;
    bool StartExposure(float duration) override;
    bool AbortExposure() override;

    void TimerHit() override;
    bool saveConfigItems(FILE *fp) override;

    virtual bool UpdateCCDFrame(int x, int y, int w, int h) override;
    virtual bool UpdateCCDBin(int binx, int biny) override;
    virtual void addFITSKeywords(INDI::CCDChip *targetChip) override;

    virtual IPState GuideNorth(uint32_t ms) override;
    virtual IPState GuideSouth(uint32_t ms) override;
    virtual IPState GuideEast(uint32_t ms) override;
    virtual IPState GuideWest(uint32_t ms) override;

    virtual bool GetFilterNames() override;
    virtual bool SetFilterNames() override;
    virtual bool SelectFilter(int) override;
    virtual int QueryFilter() override;

    INumber CoolerN[1];
    INumberVectorProperty CoolerNP;

    ISwitch CoolerS[2];
    ISwitchVectorProperty CoolerSP;

    ISwitch ShutterS[2];
    ISwitchVectorProperty ShutterSP;

    ISwitch FilterS[2];
    ISwitchVectorProperty FilterSP;

    ISwitch ReadOutS[2];
    ISwitchVectorProperty ReadOutSP;

    ISwitch GainS[3];
    ISwitchVectorProperty GainSP;
    enum { GAIN_HIGH, GAIN_LOW, GAIN_AUTO };

    ISwitch FanS[3];
    ISwitchVectorProperty FanSP;

    ISwitch ABS[2];
    ISwitchVectorProperty ABSP;

private:

    QSICamera QSICam;

    bool canAbort, canSetGain, canSetAB, canControlFan, canChangeReadoutSpeed, canFlush;

    // Filter Wheel
    int filterCount=0;
    void turnWheel();

    // Temperature
    double targetTemperature = 0;
    void activateCooler(bool enable);

    // Exposure
    struct timeval ExpStart;
    double ExposureRequest;
    void shutterControl();

    // Image Data
    int imageWidth, imageHeight;
    INDI::CCDChip::CCD_FRAME imageFrameType;
    int grabImage();

    // Timers
    int timerID;
    float CalcTimeLeft(timeval, float);

    // Misc
    bool setupParams();
    bool manageDefaults();
};

#endif // QSI_CCD_H
