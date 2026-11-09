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

        //Db
        public class CodeDocument
        {
            public string id { get; set; }
            public string code { get; set; }
            public string location { get; set; }
            public string employee_id { get; set; }
        }
    }
}
