using System.Text.Json;
using System.Text;
using Microsoft.Azure.Cosmos;
using Microsoft.Azure.Devices;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using Azure.Messaging.EventGrid;

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

            _logger.LogInformation($"Device: {payload.devId} | Received code: {payload.code}");

            var container = _cosmosClient.GetContainer(DatabaseName, CodesContainer);

            bool isValid = false;
            try
            {
                await container.ReadItemAsync<dynamic>(payload.code, new PartitionKey(StaticPartitionKey));
                isValid = true;
                _logger.LogInformation($"Device: {payload.devId} | Code: {payload.code} is VALID");
            }
            catch (CosmosException ex) when (ex.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                _logger.LogWarning($"Device: {payload.devId} | Code: {payload.code} is NOT VALID");
            }
            
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