"""Fail-closed checks for Store profile selection; no keychain writes."""
import datetime
import hashlib
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from package_appstore import profile_entitlements, use_system_crypto


class StoreProfileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.template = Path(self.temporary.name) / 'entitlements.plist'
        self.template.write_bytes(plistlib.dumps({
            'com.apple.security.app-sandbox': True,
            'com.apple.security.application-groups': ['group.test.assets'],
        }))
        self.certificate = b'test certificate only'
        self.identity = hashlib.sha1(self.certificate).hexdigest()
        self.profile = {
            'ExpirationDate': datetime.datetime.now() + datetime.timedelta(days=1),
            'DeveloperCertificates': [self.certificate],
            'Entitlements': {
                'com.apple.application-identifier': 'TEAM.test.app',
                'com.apple.developer.team-identifier': 'TEAM',
                'com.apple.security.application-groups': ['group.test.assets'],
            },
        }

    def check_profile(self):
        with patch('package_appstore.run', return_value=subprocess.CompletedProcess(
                [], 0, stdout=plistlib.dumps(self.profile))):
            return profile_entitlements(Path('test.profile'), 'test.app', 'TEAM',
                                        self.identity, self.template)

    def test_valid_profile(self):
        self.assertEqual(self.check_profile()['com.apple.application-identifier'], 'TEAM.test.app')

    def test_wrong_certificate(self):
        self.identity = '0' * 40
        with self.assertRaisesRegex(ValueError, 'certificate'):
            self.check_profile()

    def test_expired(self):
        self.profile['ExpirationDate'] = datetime.datetime(2020, 1, 1)
        with self.assertRaisesRegex(ValueError, 'Expired'):
            self.check_profile()

    def test_ungranted_group(self):
        self.profile['Entitlements']['com.apple.security.application-groups'] = []
        with self.assertRaisesRegex(ValueError, 'application group'):
            self.check_profile()

    def test_development_profile(self):
        self.profile['ProvisionedDevices'] = ['test-device']
        with self.assertRaisesRegex(ValueError, 'Store distribution'):
            self.check_profile()


class StoreCryptoTests(unittest.TestCase):
    def test_refuses_to_remove_a_live_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            tls = app / 'Contents/PlugIns/tls'
            tls.mkdir(parents=True)
            (tls / 'libqsecuretransportbackend.dylib').touch()
            plugin = tls / 'libqopensslbackend.dylib'
            plugin.touch()
            worker = app / 'worker'
            worker.touch()
            with patch('package_appstore.code_items', return_value=[worker]), patch(
                    'package_appstore.run', return_value=subprocess.CompletedProcess(
                        [], 0, stdout='@rpath/libcrypto.3.dylib')):
                with self.assertRaisesRegex(ValueError, 'OpenSSL dependency remains'):
                    use_system_crypto(app)
            self.assertTrue(plugin.exists())

    def test_requires_system_tls(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'SecureTransport'):
                use_system_crypto(Path(directory))


if __name__ == '__main__':
    unittest.main()
