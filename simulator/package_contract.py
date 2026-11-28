"""Shared extracted-evaluator assertions, independent of the native process."""
import struct

PNG_SIGNATURE = bytes.fromhex("89504e470d0a1a0a")


def require_frame(frame: bytes, instrument: str) -> None:
    if len(frame) < 33 or frame[:8] != PNG_SIGNATURE:
        raise RuntimeError(f"{instrument}: invalid PNG signature")
    if frame[8:16] != b"\x00\x00\x00\rIHDR":
        raise RuntimeError(f"{instrument}: missing PNG IHDR")
    if struct.unpack(">II", frame[16:24]) != (320, 240):
        raise RuntimeError(f"{instrument}: unexpected framebuffer dimensions")


def require_audio_rates(manifest: dict, instruments: list[dict]) -> None:
    actual = {item["id"]: item["rate"] for item in instruments}
    if len(actual) != len(instruments) or actual != manifest.get("audio_rates"):
        raise RuntimeError(
            f"Evaluator audio-rate inventory mismatch: declared "
            f"{manifest.get('audio_rates')!r}, observed {actual!r}"
        )
