#!/usr/bin/env python3
"""Run the shipping Swift bridge against Apple's local StoreKit transaction service."""
import copy
import datetime
import pathlib
import subprocess
import argparse
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--interactive-cancellation", action="store_true", help="Show Apple's local test purchase sheet; cancel it manually within 90 seconds.")
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
build = root / ".local/appstore/iap-validation/xcode"
subprocess.run(["cmake", "-S", str(root / "tests/storekit"), "-B", str(build), "-G", "Xcode"], check=True)
project = build / "StoreKitChecks.xcodeproj"
scheme = project / "xcshareddata/xcschemes/StoreKitChecks.xcscheme"
tree = ET.parse(scheme)
test_action = tree.getroot().find("TestAction")
testables = test_action.find("Testables")
if testables is not None:
    test_action.remove(testables)
testables = ET.SubElement(test_action, "Testables")
testable = ET.SubElement(testables, "TestableReference", skipped="NO")
reference = copy.deepcopy(tree.getroot().find("BuildAction/BuildActionEntries/BuildActionEntry/BuildableReference"))
reference.set("BuildableName", "StoreKitChecks.xctest")
testable.append(reference)
environment = test_action.find("EnvironmentVariables")
if environment is not None:
    test_action.remove(environment)
if args.interactive_cancellation:
    environment = ET.SubElement(test_action, "EnvironmentVariables")
    ET.SubElement(environment, "EnvironmentVariable", key="MOSS_STOREKIT_INTERACTIVE", value="1", isEnabled="YES")
    test_action.set("shouldUseLaunchSchemeArgsEnv", "NO")
tree.write(scheme, encoding="utf-8", xml_declaration=True)
result = build.parent / ("storekit-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + ".xcresult")
command = ["xcodebuild", "-project", str(project), "-scheme", "StoreKitChecks",
                "-configuration", "Debug", "-destination", "platform=macOS,arch=arm64",
                "-resultBundlePath", str(result), "DEBUG_INFORMATION_FORMAT=dwarf",
                "GCC_GENERATE_DEBUGGING_SYMBOLS=NO", "test"]
if args.interactive_cancellation:
    command += ["-only-testing:StoreKitChecks/StorePurchasesTests/testInteractiveCancellation"]
subprocess.run(command, check=True)
print(result)
