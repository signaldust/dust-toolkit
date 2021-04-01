
#pragma once

#include "window.h"

namespace dust
{
    struct AudioCallback
    {
        virtual ~AudioCallback() {}
        virtual void audioRender(float *out, unsigned ns) = 0;
    };

    // This is an Application class for programs that need a mainloop
    // Also provides IWC and exits when all attached windows are closed.
    struct Application : WindowDelegate
    {
        Application() : nOpenWindow(0), audioCallback(0) { platformInit(); }
        virtual ~Application() { platformClose(); }

        // by default, we count windows and exit when last one is closed
        void win_created() { ++nOpenWindow; }
        void win_closed() { if(!--nOpenWindow) exit(); }

        // start the application main-loop, runs until exit() is called
        void run();

        // request that application main-loop should exit
        // this causes a previous call to run() to return
        void exit();

        // called when main-loop is started for the first time
        // on OSX one should create windows from here or from
        // event handlers, since otherwise they'll be stuck in memory
        virtual void app_startup() {}

        // if callback is non-null, set the callback and open device
        // if callback is null, remove previous callback and close device
        //
        // FIXME: thread-safety when swapping a callback?
        void setAudioCallback(AudioCallback * callback)
        {
            if(callback)
            {
                bool needInit = !audioCallback;
                audioCallback = callback;
                if(needInit) platformAudioInit();
            }
            else
            {
                if(audioCallback) platformAudioClose();
                audioCallback = 0;
            }
        }

        // return current audio callback - used by platform wrappers
        AudioCallback *getAudioCallback() const { return audioCallback; }

    private:
        // this is opaque pointer to platform specific data
        // it's required for OSX to create NSPool / NSApplication
        struct PlatformData * platformData;

        unsigned nOpenWindow;

        void platformInit();
        void platformClose();

        AudioCallback   *audioCallback;

        // FIXME: these are not supported on Windows yet
        void platformAudioInit();
        void platformAudioClose();
    };
};
