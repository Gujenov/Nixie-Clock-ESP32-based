Import("env")
from SCons.Script import COMMAND_LINE_TARGETS


def _run_uploadfs_after_upload(source, target, env=None, **kwargs):
    # Запускаем после firmware upload (USB или OTA, если разрешено).
    if "upload" not in COMMAND_LINE_TARGETS or "uploadfs" in COMMAND_LINE_TARGETS:
        return

    _env = env or kwargs.get("env")
    if _env is None:
        return

    upload_protocol = str(_env.GetProjectOption("upload_protocol", "")).strip().lower()
    ota_upload_fs = str(_env.GetProjectOption("ota_upload_fs", "false")).strip().lower() in ("1", "true", "yes", "on")
    print("[post-upload] protocol={0}, ota_upload_fs={1}".format(upload_protocol or "(empty)", ota_upload_fs))
    if upload_protocol == "espota" and not ota_upload_fs:
        print("[post-upload] OTA FS upload disabled (set ota_upload_fs = true to enable)")
        return

    python = _env.subst("$PYTHONEXE")
    project = _env.subst("$PROJECT_DIR")
    pioenv = _env.subst("$PIOENV")

    print("\n[post-upload] Building SPIFFS image from data/ ...")
    rc = _env.Execute(
        '"{python}" -m platformio run -d "{project}" -e {pioenv} -t buildfs'.format(
            python=python,
            project=project,
            pioenv=pioenv,
        )
    )
    if rc != 0:
        print("[post-upload] ERROR: buildfs failed")
        _env.Exit(1)

    print("[post-upload] Uploading SPIFFS image ...")
    rc = _env.Execute(
        '"{python}" -m platformio run -d "{project}" -e {pioenv} -t uploadfs'.format(
            python=python,
            project=project,
            pioenv=pioenv,
        )
    )
    if rc != 0:
        print("[post-upload] ERROR: SPIFFS flash write failed")
        _env.Exit(1)


env.AddPostAction("upload", _run_uploadfs_after_upload)
