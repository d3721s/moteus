# -*- python -*-

# Copyright 2023 mjbots Robotic Systems, LLC.  info@mjbots.com
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//tools/workspace:github_archive.bzl", "github_archive")


def mjlib_repository(name):
    github_archive(
        name=name,
        repo="d3721s/mjlib",
        commit="dcba2c4d4c3e936948e53259d2107c03e4b3a653",
        sha256="eeff01ba118def5583653e302b732b72982f274b6f4b5c3a8f9b9c440e7828ad",
    )
