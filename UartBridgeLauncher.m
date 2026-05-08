// define class that inherits from MATLAB's System block class
classdef UartBridgeLauncher < matlab.System
    
    // all following methods can only be called by Simulink internally
    methods(Access = protected) 
        // runs once when simulation starts
        function setupImpl(obj)
            // RTW = Real Time Workshop, only true when QUARC compiles and deploys to RPi
            if coder.target('Rtw')
                // tells QUARC's C code generator to include <stdlib.h> in the generated C code
                // because system() (the C function that runs shell commands) is declared there
                coder.cinclude('<stdlib.h>'); 
     
                killCmd = ['pkill -f uart_bridge.py' 0]; // string concatenates a null terminated string
                coder.ceval('system', killCmd); // calls the C function system() with killCmd as argument
   
                startCmd = ['python3 /home/pi/uart_bridge.py > /dev/null 2>&1 &' 0];
                // > /dev/null — discard stdout (normal output)
                // 2>&1 — redirect stderr (error output) to the same place as stdout (also discarded)
                // & at the end — run in background
                coder.ceval('system', startCmd);
            end
        end

        // runs every simulation step
        // empty but exists because matlab.System requires it
        function stepImpl(obj)

        end

        // runs when simulation stops
        function releaseImpl(obj)
            if coder.target('Rtw')
                coder.cinclude('<stdlib.h>');
                termCmd = ['pkill -f uart_bridge.py' 0];
                coder.ceval('system', termCmd);
            end
        end
    end
end