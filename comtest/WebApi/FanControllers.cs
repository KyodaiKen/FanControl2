using CustomFanController;

namespace WebApi
{
    public class FanControllers
    {
        public List<FanController> Controllers { get; }

        public FanControllers(ILoggerFactory? loggerFactory = null)
        {

            var task = ControllerFactory.GetCompatibleDevicesAsync(loggerFactory);

            Controllers = task.GetAwaiter().GetResult();
        }
    }
}
