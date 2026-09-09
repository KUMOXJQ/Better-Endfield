using System.Globalization;
using BetterEndfield.UI.Models;

namespace BetterEndfield.UI.Services;

internal static class AnimationDebuggerConfiguration
{
    public static void ReadInto(IEnumerable<string> lines, ModConfiguration configuration)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        bool selected = false;
        foreach (string source in lines)
        {
            string line = source.Trim();
            if (line.Length == 0 || line.StartsWith(';') || line.StartsWith('#')) continue;
            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                selected = line[1..^1].Trim().Equals("betterendfield.animation_debugger", StringComparison.OrdinalIgnoreCase);
                continue;
            }
            int equals = line.IndexOf('=');
            if (selected && equals > 0) values[line[..equals].Trim()] = line[(equals + 1)..].Trim();
        }
        static int Integer(Dictionary<string, string> values, string key, int fallback, int max) =>
            values.TryGetValue(key, out string? raw) && int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out int value)
                ? Math.Clamp(value, 1, max) : fallback;
        configuration.AnimationDebuggerEnabled = values.TryGetValue("enabled", out string? enabled)
            && (enabled.Equals("true", StringComparison.OrdinalIgnoreCase) || enabled.Equals("yes", StringComparison.OrdinalIgnoreCase) || enabled == "1");
        configuration.AnimationSampleHz = Integer(values, "sample_hz", 30, 120);
        configuration.AnimationRefreshHz = Integer(values, "refresh_hz", 10, 30);
        configuration.AnimationMaxSeconds = Integer(values, "max_seconds", 1800, 1800);
        configuration.AnimationMaxMiB = Integer(values, "max_mib", 64, 256);
    }
}
