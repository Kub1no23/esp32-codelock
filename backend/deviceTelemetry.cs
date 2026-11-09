using Azure.Messaging.EventGrid;
using Microsoft.Azure.Cosmos;
using Microsoft.Azure.Devices;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using System.Text;
using System.Text.Json;
using static backend.Utils;

namespace backend;

public class DeviceTelemetry
{
    private readonly ILogger<DeviceTelemetry> _logger;
    private readonly CosmosClient _cosmosClient;
    private readonly ServiceClient _iotHubServiceClient;

    private const string StaticPartitionKey = "Prague";

    private readonly string DatabaseName = Environment.GetEnvironmentVariable("DatabaseName")
        ?? throw new InvalidOperationException("DatabaseName environment variable is not set");
    private readonly string CodesContainer = Environment.GetEnvironmentVariable("CodesContainer")
        ?? throw new InvalidOperationException("CodesContainer environment variable is not set");
    private readonly string LogsContainer = Environment.GetEnvironmentVariable("LogsContainer")
        ?? throw new InvalidOperationException("LogsContainer environment variable is not set");

    public DeviceTelemetry(ILogger<DeviceTelemetry> logger, CosmosClient cosmosClient, ServiceClient iotHubServiceClient)
    {
        _logger = logger;
        _cosmosClient = cosmosClient;
        _iotHubServiceClient = iotHubServiceClient;
    }

    [Function("ProcessDeviceCode")]
    public async Task ProcessDeviceCode([EventGridTrigger] EventGridEvent gridEvent)
    {
        _logger.LogInformation($"Accepted message from Event Grid: {gridEvent.EventType}");

        try
        {
            var eventData = gridEvent.Data.ToObjectFromJson<JsonElement>();
            string? base64 = eventData.GetProperty("body").GetString();
            string jsonPayload = Encoding.UTF8.GetString(Convert.FromBase64String(base64 ?? ""));

            var payload = JsonSerializer.Deserialize<Utils.CodeRequest>(jsonPayload);

            if (payload == null || string.IsNullOrEmpty(payload.devId) || string.IsNullOrEmpty(payload.code))
            {
                _logger.LogWarning("Message does not contain devId or code");
                return;
            }

            if (payload.code.Length < 4)
            {
                _logger.LogWarning($"Device: {payload.devId} | Received code shorter than 4 characters: {payload.code}");
                return;
            }

            var codesContainer = _cosmosClient.GetContainer(DatabaseName, CodesContainer);
            var logsContainer = _cosmosClient.GetContainer(DatabaseName, LogsContainer);

            _logger.LogInformation($"Device: {payload.devId} | Received code: {payload.code}");
            var receivedLog = new
            {
                id = Guid.NewGuid().ToString(),
                device_id = payload.devId,
                received_code = payload.code,
                status = "RECEIVED",
                timestamp = DateTime.UtcNow,
                partitionKey = payload.devId
            };
            await logsContainer.CreateItemAsync(receivedLog, new PartitionKey(receivedLog.partitionKey));


            bool isValid = false;
            dynamic validationLog;
            try
            {
                ItemResponse<dynamic> response = await codesContainer.ReadItemAsync<dynamic>(
                    payload.code,
                    new PartitionKey(StaticPartitionKey)
                );

                isValid = true;
                dynamic matchedCode = response.Resource;
                string empId = matchedCode.employee_id;
                string loc = matchedCode.location;

                _logger.LogInformation($"Device: {payload.devId} | Code: {payload.code} is VALID");
                validationLog = new
                {
                    id = Guid.NewGuid().ToString(),
                    device_id = payload.devId,
                    code = payload.code,
                    is_valid = true,
                    employee_id = empId,
                    location = loc,
                    status = "VALIDATED",
                    timestamp = DateTime.UtcNow,
                    partitionKey = payload.devId
                };
            }
            catch (CosmosException ex) when (ex.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                _logger.LogWarning($"Device: {payload.devId} | Code: {payload.code} is NOT VALID");
                validationLog = new
                {
                    id = Guid.NewGuid().ToString(),
                    device_id = payload.devId,
                    code = payload.code,
                    is_valid = false,
                    status = "VALIDATED",
                    timestamp = DateTime.UtcNow,
                    partitionKey = payload.devId
                };
            }

            await logsContainer.CreateItemAsync(validationLog, new PartitionKey(validationLog.partitionKey));

            var responseMessage = new Message(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(new
            {
                action = isValid ? "unlock" : "deny",
                timeout_ms = isValid ? 5000 : 0
            })));

            await _iotHubServiceClient.SendAsync(payload.devId, responseMessage);
        }
        catch (Exception ex)
        {
            _logger.LogError($"Error with processing code payload: {ex.Message}");
        }
    }
}