using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Extensions.Logging;
using System.Text.Json;
using Microsoft.Azure.Cosmos;
using Microsoft.Azure.Devices;

namespace backend;

public class Frontend
{
    private readonly ILogger<Frontend> _logger;
    private readonly CosmosClient _cosmosClient;
    private readonly ServiceClient _iotHubServiceClient;
    private readonly string DatabaseName = Environment.GetEnvironmentVariable("DatabaseName") 
        ?? throw new InvalidOperationException("DatabaseName environment variable is not set");
    private readonly string DevicesContainer = Environment.GetEnvironmentVariable("DevicesContainer") 
        ?? throw new InvalidOperationException("DevicesContainer environment variable is not set");
    private readonly string LogsContainer = Environment.GetEnvironmentVariable("LogsContainer") 
        ?? throw new InvalidOperationException("LogsContainer environment variable is not set");

    public Frontend(ILogger<Frontend> logger, CosmosClient cosmosClient, ServiceClient iotHubClient)
    {
        _logger = logger;
        _cosmosClient = cosmosClient;
        _iotHubServiceClient = iotHubClient;
    }

    [Function("GetDevices")]
    public async Task<IActionResult> GetDevices([HttpTrigger(AuthorizationLevel.Function, "get", Route = "devices")] HttpRequest req)
    {
        _logger.LogInformation("Getting list of all devices");
        var container = _cosmosClient.GetContainer(DatabaseName, DevicesContainer);
        var query = new QueryDefinition("SELECT * FROM c");

        var results = new List<dynamic>();
        using var iterator = container.GetItemQueryIterator<dynamic>(query);
        while (iterator.HasMoreResults)
        {
            var response = await iterator.ReadNextAsync();
            results.AddRange(response.Resource);
        }

        return new OkObjectResult(results);
    }

    [Function("GetDeviceLogs")]
    public async Task<IActionResult> GetDeviceLogs(
        [HttpTrigger(AuthorizationLevel.Function, "get", Route = "devices/{devId}/logs")] HttpRequest req,
        string devId)
    {
        _logger.LogInformation($"Device: {devId} | Getting last 20 logs");
        var container = _cosmosClient.GetContainer(DatabaseName, LogsContainer);

        var query = new QueryDefinition("SELECT TOP 20 * FROM c WHERE c.device_id = @devId ORDER BY c._ts DESC")
            .WithParameter("@devId", devId);

        var results = new List<dynamic>();

        using var iterator = container.GetItemQueryIterator<dynamic>(query, requestOptions: new QueryRequestOptions { PartitionKey = new PartitionKey(devId) });

        while (iterator.HasMoreResults)
        {
            var response = await iterator.ReadNextAsync();
            results.AddRange(response.Resource);
        }

        return new OkObjectResult(results);
    }

    [Function("GetDeviceStatus")]
    public async Task<IActionResult> GetDeviceStatus(
        [HttpTrigger(AuthorizationLevel.Function, "get", Route = "devices/{devId}/status")] HttpRequest req,
        string devId)
    {
        _logger.LogInformation($"Device: {devId} | Getting status through IoT Hub Direct Method");

        try
        {
            var methodInvocation = new CloudToDeviceMethod("get_status")
            {
                ResponseTimeout = TimeSpan.FromSeconds(15) //timeout till abort
            };

            CloudToDeviceMethodResult response = await _iotHubServiceClient.InvokeDeviceMethodAsync(devId, methodInvocation);

            return new OkObjectResult(new
            {
                deviceStatus = response.Status,
                payload = response.GetPayloadAsJson()
            });
        }
        catch (Exception ex)
        {
            _logger.LogError($"Device: {devId} | Not responding or offline: {ex.Message}");
            return new StatusCodeResult(StatusCodes.Status504GatewayTimeout);
        }
    }

    [Function("PostDeviceSignal")]
    public async Task<IActionResult> PostDeviceSignal(
        [HttpTrigger(AuthorizationLevel.Function, "post", Route = "devices/{devId}/signal")] HttpRequest req,
        [FromBody] Utils.SignalRequest payload,
        string devId)
    {
        if (payload.signalType == null)
        {
            _logger.LogError($"Device: {devId} | Missing parameter 'signalType' in request body");
            return new BadRequestObjectResult("Missing parameter 'signalType' in request body");
        }

        string methodName = "";
        string payloadString = "{}";

        switch (payload.signalType)
        {
            case Utils.SignalType.Open:
                methodName = "open_lock";
                payloadString = JsonSerializer.Serialize(new { timeout_ms = 5000 });
                break;

            case Utils.SignalType.Restart:
                methodName = "reboot_device";
                payloadString = JsonSerializer.Serialize(new { reason = "user_requested_restart" });
                break;

            case Utils.SignalType.Shutdown:
                methodName = "deep_sleep";
                payloadString = JsonSerializer.Serialize(new { sleep_time_ms = 0 });
                break;

            default:
                _logger.LogError($"Device: {devId} | Unknown signalType: '{payload.signalType}'");
                return new BadRequestObjectResult($"Unknown signalType: '{payload.signalType}'");
        }
        _logger.LogInformation($"Device: {devId} | Received signal: {payload.signalType}");

        try
        {
            var methodInvocation = new CloudToDeviceMethod(methodName) { ResponseTimeout = TimeSpan.FromSeconds(15) };
            methodInvocation.SetPayloadJson(payloadString);

            var response = await _iotHubServiceClient.InvokeDeviceMethodAsync(devId, methodInvocation);
            return new OkObjectResult(new { deviceStatus = response.Status, response = response.GetPayloadAsJson() });
        }
        catch (Exception ex)
        {
            _logger.LogError($"Device: {devId} | Error in IoT Hub communication: {ex.Message}");
            return new StatusCodeResult(StatusCodes.Status504GatewayTimeout);
        }
    }
}