"""Waveform checks for fixed real voicebank acceptance fixtures."""
import math

def voice_metrics(pcm):
    values = [v / 32768 for v in pcm[::2]]
    assert values, "Voice fixture was empty"
    return dict(peak=max(abs(v) for v in values),
                rms=math.sqrt(sum(v*v for v in values) / len(values)),
                dc=sum(values) / len(values),
                nearFullScale=sum(abs(v) > .95 for v in values) / len(values))

def check_voice(pcm):
    q = voice_metrics(pcm)
    # These fixtures have unity gain and neutral controls. The regression that
    # sent absolute 0 dB to the model produced RMS .65-.78 and saturated plateaus.
    assert .001 < q["rms"] < .3, f"Invalid native singing level: {q}"
    assert q["peak"] < .85 and q["nearFullScale"] == 0, f"Saturated native singing: {q}"
    assert abs(q["dc"]) < .005, f"Large DC offset: {q}"
    return q
