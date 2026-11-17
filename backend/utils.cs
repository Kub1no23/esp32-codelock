using System;
using System.Collections.Generic;
using System.Text;

namespace Backend.Utils
{
    public class Utils
    {
        public enum SignalType
        {
            Open,
            Restart
        }


        //DTO
        public record SignalRequest(SignalType? signalType);
        public record CodeRequest(string devId, string code);

        //DB
        public class CodeDocument
        {
            public string id { get; set; }
            public string location { get; set; }
            public string employee_id { get; set; }
        }
        public class DeviceModel
        {
            public string id { get; set; }
            public string location { get; set; }
        }
    }
}
