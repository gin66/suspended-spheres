Import("env")
import os

# The workflow sets SS_FW_VERSION and SS_BUILD_ID from the annotated tag and
# the tagged commit.  A bench build sets SS_FW_DIRTY=1.  Environment values
# arrive as raw strings, so the version quote is added here.
ver = os.environ.get("SS_FW_VERSION")
build = os.environ.get("SS_BUILD_ID")
dirty = os.environ.get("SS_FW_DIRTY")

flags = []
if ver:
    flags.append('-DSS_FW_VERSION=\\"%s\\"' % ver)
if build:
    flags.append("-DSS_BUILD_ID=%s" % build)
if dirty and dirty != "0":
    flags.append("-DSS_FW_DIRTY=1")
release = os.environ.get("SS_RELEASE")
if release and release != "0":
    flags.append("-DSS_RELEASE=1")
if flags:
    env.Append(BUILD_FLAGS=flags)
