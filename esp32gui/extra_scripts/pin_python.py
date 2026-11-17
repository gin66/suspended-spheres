Import("env")
import os

# The pioarduino hybrid Arduino build starts a child `pio run` using
# dirname($PYTHONEXE)/pio.  The CI runner's preinstalled Python can point at
# a different PlatformIO Core, so the workflow supplies the pinned venv.
p = os.environ.get("SS_PYTHONEXE")
if p:
    env.Replace(PYTHONEXE=p)
    print("pin_python: PYTHONEXE=%s" % p)
