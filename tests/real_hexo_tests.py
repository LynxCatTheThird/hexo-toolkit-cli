"""Run real Hexo against local generators and deployers; no remote deployment occurs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
RUNTIME = Path(sys.argv.pop(1)).resolve()


class RealHexo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='hexo integration ')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.project = Path(cls.temp.name) / 'blog'
        cls.project.mkdir()
        # Copy once for the suite, including .bin shims, so npm and Node resolve real project dependencies.
        shutil.copytree(RUNTIME / 'node_modules', cls.project / 'node_modules', symlinks=True)
        plugin = cls.project / 'node_modules' / 'hexo-swpp'
        plugin.mkdir(exist_ok=True)
        (plugin / 'package.json').write_text(json.dumps({'name': 'hexo-swpp', 'version': '1.0.0',
                                                      'main': 'index.js'}), encoding='utf-8')
        (plugin / 'index.js').write_text("""
const fs = require('fs');
hexo.extend.console.register('swpp', 'local test command', {}, function(args) {
  fs.appendFileSync('calls.jsonl', JSON.stringify({phase: 'plugin', target: hexo.config.target,
    publicDir: hexo.config.public_dir, args}) + '\\n');
});
""", encoding='utf-8')

    def setUp(self):
        for entry in self.project.iterdir():
            if entry.name == 'node_modules':
                continue
            if entry.is_dir():
                shutil.rmtree(entry)
            else:
                entry.unlink()
        manifest = {'name': 'local-hexo-fixture', 'private': True, 'hexo': {'version': '7.3.0'},
                    'dependencies': {'hexo': '7.3.0', 'hexo-swpp': '1.0.0'}}
        (self.project / 'package.json').write_text(json.dumps(manifest), encoding='utf-8')
        self.write_config('deploy:\n  type: local-test\n')
        scripts = self.project / 'scripts'
        scripts.mkdir()
        (scripts / 'local-test.js').write_text("""
const fs = require('fs');
function record(phase, args) {
  fs.appendFileSync('calls.jsonl', JSON.stringify({phase, target: hexo.config.target,
    publicDir: hexo.config.public_dir, args}) + '\\n');
}
hexo.extend.generator.register('local-test', function() {
  record('generate', {});
  return {path: 'review.txt', data: hexo.config.target};
});
if (hexo.env.cmd === 'deploy') for (const type of ['local-test', 'second-test']) {
  hexo.extend.deployer.register(type, function(args) {
    record('deploy', args);
    if (hexo.config.fail_deploy) throw new Error('LOCAL_DEPLOY_FAILURE');
  });
}
""", encoding='utf-8')
        self.env = dict(os.environ)
        for key in ('HEXO_TOOLKIT_DEBUG', 'HEXO_TOOLKIT_TRACE'):
            self.env.pop(key, None)

    def write_config(self, deploy, name='_config.yml', target='default'):
        (self.project / name).write_text('title: test\nurl: https://example.invalid\n' +
                                        f'target: {target}\npublic_dir: public-{target}\n' + deploy,
                                        encoding='utf-8')

    def run_cli(self, *args, success=True):
        result = subprocess.run([str(BINARY), '--no-color', '--project', str(self.project), *args],
                                env=self.env, capture_output=True, text=True, encoding='utf-8', timeout=40)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stderr)
            self.assertNotIn('✓ 部署完成', result.stderr)
        return result

    def calls(self):
        path = self.project / 'calls.jsonl'
        return [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()] if path.exists() else []

    def test_valid_deployment_and_cleanup(self):
        self.run_cli('deploy')
        self.assertEqual([entry['phase'] for entry in self.calls()], ['generate', 'plugin', 'deploy'])
        self.assertFalse((self.project / 'public-default').exists())

    def test_unconfigured_deployment_fails_and_keeps_output(self):
        for setting in ('', 'deploy: {}\n', 'deploy: []\n', 'deploy:\n  type: ""\n'):
            with self.subTest(setting=setting):
                self.write_config(setting)
                (self.project / 'calls.jsonl').unlink(missing_ok=True)
                result = self.run_cli('deploy', success=False)
                self.assertIn('No deployer configured', result.stderr)
                self.assertEqual([entry['phase'] for entry in self.calls()], ['generate', 'plugin'])
                self.assertTrue((self.project / 'public-default' / 'review.txt').exists())

    def test_missing_deployer_fails_before_any_deployment(self):
        for setting in ('deploy:\n  type: missing-plugin\n',
                        'deploy:\n  - type: local-test\n  - type: missing-plugin\n'):
            with self.subTest(setting=setting):
                self.write_config(setting)
                (self.project / 'calls.jsonl').unlink(missing_ok=True)
                result = self.run_cli('deploy', success=False)
                self.assertIn('Deployer not found: missing-plugin', result.stderr)
                self.assertFalse(any(entry['phase'] == 'deploy' for entry in self.calls()))
                self.assertTrue((self.project / 'public-default' / 'review.txt').exists())

    def test_multiple_registered_deployers(self):
        self.write_config('deploy:\n  - type: local-test\n  - type: second-test\n')
        self.run_cli('deploy')
        self.assertEqual([entry['args']['type'] for entry in self.calls() if entry['phase'] == 'deploy'],
                         ['local-test', 'second-test'])

    def test_deployer_exception_keeps_output(self):
        self.write_config('deploy:\n  type: local-test\nfail_deploy: true\n')
        result = self.run_cli('deploy', success=False)
        self.assertIn('LOCAL_DEPLOY_FAILURE', result.stderr)
        self.assertTrue((self.project / 'public-default' / 'review.txt').exists())

    def test_custom_config_is_shared_by_all_hexo_commands(self):
        self.write_config('deploy:\n  type: missing-default-plugin\n')
        self.write_config('deploy:\n  type: local-test\n', name='site staging.yml', target='staging')
        self.write_config('deploy:\n  type: local-test\n', name='staging.yml', target='staging')
        for arguments in [('--config', 'site staging.yml'), ('--config=site staging.yml',),
                          ('--config', '_config.yml,staging.yml')]:
            with self.subTest(arguments=arguments):
                (self.project / 'calls.jsonl').unlink(missing_ok=True)
                self.run_cli('deploy', '--', *arguments)
                calls = self.calls()
                self.assertEqual([entry['phase'] for entry in calls], ['generate', 'plugin', 'deploy'])
                self.assertEqual({entry['target'] for entry in calls}, {'staging'})
                self.assertEqual({entry['publicDir'] for entry in calls}, {'public-staging'})


if __name__ == '__main__':
    unittest.main()
