classdef UartBridgeLauncher < matlab.System
    
    methods(Access = protected)
        function setupImpl(obj)
            if coder.target('Rtw')
                coder.cinclude('<stdlib.h>');
     
                killCmd = ['pkill -f uart_bridge.py' 0];
                coder.ceval('system', killCmd);
   
                startCmd = ['python3 /home/pi/uart_bridge.py > /dev/null 2>&1 &' 0];
                coder.ceval('system', startCmd);
            end
        end

        function stepImpl(obj)

        end

        function releaseImpl(obj)
            if coder.target('Rtw')
                coder.cinclude('<stdlib.h>');
                termCmd = ['pkill -f uart_bridge.py' 0];
                coder.ceval('system', termCmd);
            end
        end
    end
end