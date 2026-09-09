using BetterEndfield.UI.Models;
using BetterEndfield.UI.Services;

static void Check(bool value, string message)
{
    if (!value) throw new InvalidOperationException(message);
}
var original = new ModConfiguration
{
    AnimationDebuggerEnabled = true, AnimationSampleHz = 60, AnimationRefreshHz = 20,
    AnimationMaxSeconds = 900, AnimationMaxMiB = 128, FreeCameraEnabled = false
};
string ini = original.ToIni();
var loaded = ModConfiguration.CreateDefaults();
AnimationDebuggerConfiguration.ReadInto(ini.Split('\n'), loaded);
Check(loaded.AnimationDebuggerEnabled && loaded.AnimationSampleHz == 60 && loaded.AnimationRefreshHz == 20
    && loaded.AnimationMaxSeconds == 900 && loaded.AnimationMaxMiB == 128, "Full configuration save/load must preserve animation settings");
AnimationDebuggerConfiguration.ReadInto((ini + "\n[another.module]\nenabled=false\nsample_hz=1\n").Split('\n'), loaded);
Check(loaded.AnimationDebuggerEnabled && loaded.AnimationSampleHz == 60, "Other sections must not override debugger keys");
AnimationDebuggerConfiguration.ReadInto("[BETTERENDFIELD.ANIMATION_DEBUGGER]\nenabled=YES\nsample_hz=999\nrefresh_hz=NaN\nmax_seconds=-1\nmax_mib=0".Split('\n'), loaded);
Check(loaded.AnimationDebuggerEnabled && loaded.AnimationSampleHz == 120 && loaded.AnimationRefreshHz == 10
    && loaded.AnimationMaxSeconds == 1 && loaded.AnimationMaxMiB == 1, "Invalid values must be bounded or defaulted");
AnimationDebuggerConfiguration.ReadInto(["[betterendfield.camera]", "enabled=true"], loaded);
Check(!loaded.AnimationDebuggerEnabled && loaded.AnimationSampleHz == 30, "Debugger remains disabled when its section is absent");
Check(ini.Split("[Launcher]").Length == 2, "Launcher section not duplicated");
Console.WriteLine("Animation configuration round-trip and section isolation checks passed");
