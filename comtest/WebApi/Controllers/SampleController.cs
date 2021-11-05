using CustomFanController;
using Microsoft.AspNetCore.Mvc;

namespace WebApi.Controllers
{
    [ApiController]
    [Route("[controller]")]
    public class SampleController : ControllerBase
    {
        private readonly ILogger<SampleController> Logger;
        private readonly List<FanController> FanControllers;

        public SampleController(ILogger<SampleController> Logger, FanControllers FanControllers)
        {
            this.Logger = Logger;
            this.FanControllers = FanControllers.Controllers;
        }

        [HttpGet(nameof(GetAvailableDevices))]
        public IEnumerable<string> GetAvailableDevices()
        {
            var available = new List<string>();

            foreach (var controller in FanControllers)
            {
                available.Add(controller.DeviceName);
            }

            return available;
        }

        [HttpGet(nameof(GetReadings))]
        public async Task<IEnumerable<Readings>> GetReadings()
        {
            var readings = new List<Readings>();

            foreach (var controller in FanControllers)
            {
                readings.Add(await controller.GetReadings());
            }

            return readings;
        }
    }
}