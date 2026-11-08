using Azure.Monitor.OpenTelemetry.Exporter;
using Microsoft.Azure.Cosmos;
using Microsoft.Azure.Functions.Worker;
using Microsoft.Azure.Functions.Worker.Builder;
using Microsoft.Azure.Functions.Worker.OpenTelemetry;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using OpenTelemetry;
using Microsoft.Azure.Devices;

var builder = FunctionsApplication.CreateBuilder(args);

builder.ConfigureFunctionsWebApplication();

builder.Services.AddOpenTelemetry()
    .UseFunctionsWorkerDefaults()
    .UseAzureMonitorExporter();

builder.Services.AddSingleton(s =>
{
    string cosmosConnString = Environment.GetEnvironmentVariable("CosmosDbConnectionString") 
        ?? throw new InvalidOperationException("CosmosDbConnectionString environment variable is not set");

    return new CosmosClient(cosmosConnString);
});

builder.Services.AddSingleton(s =>
{
    string iotHubConnString = Environment.GetEnvironmentVariable("IotHubConnectionString") 
        ?? throw new InvalidOperationException("IotHubConnectionString environment variable is not set");

    return ServiceClient.CreateFromConnectionString(iotHubConnString);
});

builder.Build().Run();
