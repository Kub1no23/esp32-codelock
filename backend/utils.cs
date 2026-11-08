using System;
using System.Collections.Generic;
using System.Text;

namespace backend
{
    public class Utils
    {
        public enum SignalType
        {
            Open,
            Restart,
            Shutdown
        }

        //DTO
        public record SignalRequest(SignalType? signalType);
        public record CodeRequest(string devId, string code);
    }
}
