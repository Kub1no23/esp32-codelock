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
        public class SignalRequest
        {
            public SignalType? signalType { get; set; }
        }
        public record CodeRequest(string devId, string code);

        //DB
        public class CodeDocument
        {
            public required string id { get; set; }
            public required string location { get; set; }
            public required string employee_id { get; set; }
        }
        public class DeviceModel
        {
            public required string id { get; set; }
            public required string location { get; set; }
        }
    }
}
