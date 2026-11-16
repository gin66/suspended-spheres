Import("env")
import os

ver = os.environ.get("SS_FW_VERSION")
build = os.environ.get("SS_BUILD_ID")
dirty = os.environ.get("SS_FW_DIRTY")
release = os.environ.get("SS_RELEASE")
owner = os.environ.get("SS_GITHUB_OWNER")
repo = os.environ.get("SS_GITHUB_REPO")

flags = []
if ver:
    flags.append('-DSS_FW_VERSION=\\"%s\\"' % ver)
if build:
    flags.append("-DSS_BUILD_ID=%s" % build)
if dirty and dirty != "0":
    flags.append("-DSS_FW_DIRTY=1")
if release and release != "0":
    flags.append("-DSS_RELEASE=1")
if owner:
    flags.append('-DSS_GITHUB_OWNER=\\"%s\\"' % owner)
if repo:
    flags.append('-DSS_GITHUB_REPO=\\"%s\\"' % repo)
if flags:
    env.Append(BUILD_FLAGS=flags)
