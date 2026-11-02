"""Exercise the CLI with local fake package managers; never install or deploy anything."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import signal
import shlex
import shutil
import unittest


def mock_command():
    args = sys.argv[3:]
    with open(os.environ['HEXO_TEST_CALLS'], 'a', encoding='utf-8') as calls:
        calls.write(json.dumps(args) + '\n')
    if 'server' in args:
        print('server listening on configured address', flush=True)
    if os.environ.get('HEXO_TEST_PROMPT') and 'swpp' in args:
        answer = input('CONFIRM_PLUGIN [y/N]: ')
        print('answer=' + answer, flush=True)
    if os.environ.get('HEXO_TEST_LARGE'):
        sys.stdout.write('x' * 2_000_000 + '\nEND_OF_OUTPUT\n')
    else:
        print('child stdout', flush=True)
        print('child stderr', file=sys.stderr, flush=True)
    if os.environ.get('HEXO_TEST_WAIT'):
        time.sleep(60)
    if os.environ.get('HEXO_TEST_STDIN'):
        print('stdin=' + sys.stdin.read(), flush=True)
        print('CI=' + os.environ.get('CI', '<unset>'), flush=True)
    if (os.environ.get('HEXO_TEST_FAIL') in args or
            (os.environ.get('HEXO_TEST_FAIL') == 'deploy' and '--eval' in args)):
        return 7
    return 0


if len(sys.argv) > 1 and sys.argv[1] == '--mock':
    sys.exit(mock_command())

BINARY = Path(sys.argv.pop(1)).resolve()


class Workflows(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='hexo workflow ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'blog'
        self.project.mkdir()
        (self.project / '_config.yml').write_text('title: test\n', encoding='utf-8')
        self.manifest = {'dependencies': {'hexo-swpp': '1'}}
        self.save_manifest()
        self.calls = self.root / 'calls.jsonl'
        self.env = dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'],
                        HEXO_TEST_CALLS=str(self.calls))
        for key in ('HEXO_TOOLKIT_DEBUG', 'HEXO_TOOLKIT_TRACE'):
            self.env.pop(key, None)
        for manager in ('npx', 'npm', 'pnpm', 'yarn', 'git', 'node', 'formatter'):
            if os.name == 'nt':
                wrapper = self.root / (manager + '.cmd')
                wrapper.write_text(f'@"{sys.executable}" "{Path(__file__).resolve()}" --mock {manager} %*\r\n')
            else:
                import shlex
                wrapper = self.root / manager
                wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(sys.executable) + ' ' +
                                   shlex.quote(str(Path(__file__).resolve())) + ' --mock ' + manager + ' "$@"\n')
                wrapper.chmod(0o755)

    def save_manifest(self):
        (self.project / 'package.json').write_text(json.dumps(self.manifest), encoding='utf-8')

    def run_cli(self, *args, code=0, **kwargs):
        result = subprocess.run([str(BINARY), *args, '--project', str(self.project)] if '--' not in args else
                                [str(BINARY), '--project', str(self.project), *args],
                                cwd=self.root, env=self.env, text=True, encoding='utf-8',
                                capture_output=True, timeout=20, **kwargs)
        self.assertEqual(result.returncode, code, result.stderr)
        return result

    def commands(self):
        return [json.loads(line) for line in self.calls.read_text(encoding='utf-8').splitlines()] if self.calls.exists() else []

    def test_deploy_order(self):
        self.run_cli('deploy')
        calls = self.commands()
        self.assertEqual([args[2] for args in calls[:3]], ['clean', 'generate', 'swpp'])
        self.assertEqual(calls[3][0], '--eval')
        self.assertEqual(calls[4][2], 'clean')
        self.assertEqual(len(calls), 5)

    def test_deployment_failure_skips_final_cleanup(self):
        self.env['HEXO_TEST_FAIL'] = 'deploy'
        self.run_cli('deploy', code=7)
        self.assertEqual(len(self.commands()), 4)
        self.assertEqual(self.commands()[-1][0], '--eval')

    def test_checked_deploy_uses_package_manager_node_environment(self):
        for manager, prefix in [('pnpm', ['exec', 'node']), ('yarn', ['node'])]:
            with self.subTest(manager=manager):
                self.manifest['packageManager'] = manager + '@4.0.0'
                self.save_manifest()
                self.calls.unlink(missing_ok=True)
                self.run_cli('deploy')
                self.assertEqual(self.commands()[3][:len(prefix) + 1], prefix + ['--eval'])

    def test_generation_failure_stops_deploy(self):
        self.env['HEXO_TEST_FAIL'] = 'generate'
        result = self.run_cli('deploy', code=7)
        self.assertEqual([args[2] for args in self.commands()], ['clean', 'generate'])
        self.assertIn('child stderr', result.stderr)

    def test_plugin_failure_stops_deploy(self):
        self.env['HEXO_TEST_FAIL'] = 'swpp'
        self.run_cli('deploy', code=7)
        self.assertEqual([args[2] for args in self.commands()], ['clean', 'generate', 'swpp'])

    def test_typo_only_suggests(self):
        result = self.run_cli('deply', code=1)
        self.assertIn('deploy', result.stderr)
        self.assertFalse(self.calls.exists())

    def test_help_after_command(self):
        result = self.run_cli('build', '--help')
        self.assertIn('HexoTool', result.stdout)
        self.assertFalse(self.calls.exists())

    def test_server_custom_address_and_log(self):
        result = self.run_cli('server', '--log-file', 'server.log', '--', '--port', '5123', '--host', '::1')
        self.assertEqual(self.commands(), [['--no-install', 'hexo', 'server', '--port', '5123', '--host', '::1']])
        self.assertIn('server listening', result.stderr)
        self.assertIn('child stderr', (self.root / 'server.log').read_text(encoding='utf-8'))
        self.assertFalse((self.project / 'server.log').exists())

    def test_server_failure_visible(self):
        self.env['HEXO_TEST_FAIL'] = 'server'
        result = self.run_cli('srv', code=7)
        self.assertIn('child stderr', result.stderr)
        self.assertEqual(len(self.commands()), 1)

    def test_quiet_server_failure_shows_tail(self):
        self.env['HEXO_TEST_FAIL'] = 'server'
        result = self.run_cli('server', '--quiet', code=7)
        self.assertIn('child stderr', result.stderr)

    def test_pnpm_direct_plugin(self):
        (self.project / 'pnpm-lock.yaml').write_text('lockfileVersion: 9\n')
        self.run_cli('build')
        self.assertEqual(self.commands()[-1], ['exec', 'hexo', 'swpp'])

    def test_conflicting_locks_rejected(self):
        (self.project / 'pnpm-lock.yaml').touch()
        (self.project / 'yarn.lock').touch()
        self.run_cli('build', code=1)
        self.assertFalse(self.calls.exists())

    def test_declared_manager_wins(self):
        self.manifest['packageManager'] = 'pnpm@10.0.0'
        self.save_manifest()
        (self.project / 'yarn.lock').touch()
        self.run_cli('build')
        self.assertEqual(self.commands()[0][:2], ['exec', 'hexo'])

    def test_modern_yarn_upgrade(self):
        self.manifest['packageManager'] = 'yarn@4.0.0'
        self.save_manifest()
        self.run_cli('pkg')
        self.assertEqual(self.commands(), [['up', '*', '@*/*']])

    def test_classic_yarn_upgrade(self):
        self.manifest['packageManager'] = 'yarn@1.22.0'
        self.save_manifest()
        self.run_cli('packages')
        self.assertEqual(self.commands(), [['upgrade', '--latest']])

    def test_log_open_failure_prevents_execution(self):
        self.run_cli('build', '--log-file', 'missing/path.log', code=1)
        self.assertFalse(self.calls.exists())

    @unittest.skipUnless(os.name == 'posix' and Path('/dev/full').exists(), 'requires /dev/full')
    def test_log_write_failure_prevents_execution(self):
        self.run_cli('build', '--log-file', '/dev/full', code=1)
        self.assertFalse(self.calls.exists())

    def test_large_output_is_logged_without_deadlock(self):
        self.env['HEXO_TEST_LARGE'] = '1'
        self.env['HEXO_TEST_FAIL'] = 'clean'
        result = self.run_cli('build', '--log-file', 'large.log', code=7)
        self.assertIn('END_OF_OUTPUT', result.stderr)
        self.assertLess(len(result.stderr), 10000)
        self.assertGreater((self.root / 'large.log').stat().st_size, 2_000_000)

    def test_passthrough_arguments_preserved(self):
        self.run_cli('clean', '--', '--config', 'site config.yml', "a'b")
        self.assertEqual(self.commands()[0][-3:], ['--config', 'site config.yml', "a'b"])

    def test_hexo_plugins_share_configuration_without_generation_flags(self):
        for shared in [('--config', 'site config.yml'), ('--config=site config.yml',),
                       ('--config', 'base.yml,staging.yml'), ('--cwd', 'other blog', '--safe')]:
            with self.subTest(shared=shared):
                self.calls.unlink(missing_ok=True)
                self.run_cli('build', '--', *shared, '--bail')
                self.assertEqual(self.commands()[-1], ['--no-install', 'hexo', *shared, 'swpp'])
                self.assertIn('--bail', self.commands()[1])

    def test_non_hexo_tools_do_not_receive_hexo_options(self):
        executable = self.root / BINARY.name
        shutil.copy2(BINARY, executable)
        (self.root / 'config.yaml').write_text('additionalTools:\n  - [hexo-swpp, "formatter --check"]\n')
        result = subprocess.run([str(executable), 'build', '--project', str(self.project),
                                 '--', '--config', 'site config.yml'], env=self.env,
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.commands()[-1], ['--no-install', 'formatter', '--check'])

    def test_shared_config_stays_on_hexo_in_compound_tool(self):
        executable = self.root / BINARY.name
        shutil.copy2(BINARY, executable)
        (self.root / 'config.yaml').write_text(
            'additionalTools:\n  - [hexo-swpp, "hexo swpp && formatter --check"]\n')
        result = subprocess.run([str(executable), 'build', '--project', str(self.project),
                                 '--', '--config', 'site config.yml'], env=self.env,
                                capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.commands()[-2], ['--no-install', 'hexo', '--config', 'site config.yml', 'swpp'])
        self.assertEqual(self.commands()[-1], ['--check'])

    def test_missing_shared_option_value_prevents_cleanup(self):
        self.run_cli('build', '--', '--config', code=1)
        self.assertFalse(self.calls.exists())

    def test_stdin_and_environment_preserved(self):
        self.env['HEXO_TEST_STDIN'] = '1'
        self.env['CI'] = 'user-value'
        result = self.run_cli('theme', input='hello\n')
        self.assertIn('stdin=hello', result.stderr)
        self.assertIn('CI=user-value', result.stderr)

    @unittest.skipIf(os.name == 'nt', 'POSIX signals and terminal ownership')
    def test_server_ctrl_c(self):
        self.env['HEXO_TEST_WAIT'] = '1'
        process = subprocess.Popen([str(BINARY), 'server', '--project', str(self.project)],
                                   cwd=self.root, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 10
            while not self.calls.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.calls.exists(), 'server command did not start')
            process.send_signal(signal.SIGINT)
            _, stderr = process.communicate(timeout=10)
            self.assertEqual(process.returncode, 0, stderr)
        finally:
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=10)

    @unittest.skipIf(os.name == 'nt', 'POSIX pseudo-terminal')
    def test_interactive_terminal_input(self):
        import pty
        import select
        self.env['HEXO_TEST_STDIN'] = '1'
        pid, descriptor = pty.fork()
        if pid == 0:
            result = subprocess.run([str(BINARY), 'theme', '--project', str(self.project)], env=self.env)
            restored = os.tcgetpgrp(0) == os.getpgrp()
            os._exit(result.returncode if result.returncode else (0 if restored else 3))
        output = b''
        sent_input = False
        exited = False
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if select.select([descriptor], [], [], 0.1)[0]:
                    try:
                        chunk = os.read(descriptor, 16384)
                    except OSError:
                        chunk = b''
                    if not chunk:
                        time.sleep(.01)
                    output += chunk
                    if b'child stdout' in output and not sent_input:
                        os.write(descriptor, b'hello\n\x04')
                        sent_input = True
                result, status = os.waitpid(pid, os.WNOHANG)
                if result:
                    exited = True
                    self.assertEqual(os.waitstatus_to_exitcode(status), 0, output)
                    break
            if not exited:
                result, status = os.waitpid(pid, os.WNOHANG)
                exited = result != 0
                if exited:
                    self.assertEqual(os.waitstatus_to_exitcode(status), 0, output)
            self.assertTrue(exited, output)
            self.assertIn(b'stdin=hello', output)
        finally:
            if not exited:
                os.kill(pid, signal.SIGTERM)
                os.waitpid(pid, 0)
            os.close(descriptor)

    @unittest.skipIf(os.name == 'nt', 'POSIX pseudo-terminal')
    def test_build_plugin_prompt_is_visible(self):
        import pty
        import select
        self.env['HEXO_TEST_PROMPT'] = '1'
        for options in [[], ['--quiet']]:
            with self.subTest(options=options):
                pid, descriptor = pty.fork()
                if pid == 0:
                    os.execve(str(BINARY), [str(BINARY), 'build', *options,
                                           '--project', str(self.project)], self.env)
                output = b''
                answered = False
                exited = False
                try:
                    deadline = time.monotonic() + 10
                    while time.monotonic() < deadline:
                        if select.select([descriptor], [], [], .1)[0]:
                            try:
                                output += os.read(descriptor, 16384)
                            except OSError:
                                # PTY EOF can arrive just before waitpid reports the child's exit.
                                time.sleep(.01)
                        if b'CONFIRM_PLUGIN' in output and not answered:
                            os.write(descriptor, b'y\n')
                            answered = True
                        result, status = os.waitpid(pid, os.WNOHANG)
                        if result:
                            exited = True
                            self.assertEqual(os.waitstatus_to_exitcode(status), 0, output)
                            break
                    if not exited:
                        result, status = os.waitpid(pid, os.WNOHANG)
                        exited = result != 0
                        if exited:
                            self.assertEqual(os.waitstatus_to_exitcode(status), 0, output)
                    self.assertTrue(answered and exited, output)
                finally:
                    if not exited:
                        os.kill(pid, signal.SIGTERM)
                        os.waitpid(pid, 0)
                    os.close(descriptor)

    @unittest.skipUnless(os.name == 'posix' and shutil.which('bash'), 'requires a job-control shell')
    def test_server_ctrl_z_fg_and_bg(self):
        import pty
        import select
        self.env.update(HEXO_TEST_WAIT='1', PS1='HEXO_TEST_SHELL> ', TERM='dumb')
        pid, descriptor = pty.fork()
        if pid == 0:
            os.execve(shutil.which('bash'), ['bash', '--noprofile', '--norc', '-i'], self.env)
        output = b''
        child_group = None
        wrapper_group = None

        def read_until(marker):
            nonlocal output
            deadline = time.monotonic() + 10
            while marker not in output and time.monotonic() < deadline:
                if select.select([descriptor], [], [], .1)[0]:
                    output += os.read(descriptor, 16384)
            self.assertIn(marker, output)
            output = b''

        try:
            read_until(b'HEXO_TEST_SHELL> ')
            wrapper_pid_file = self.root / 'wrapper.pid'
            command = ('printf "%s" "$$" > ' + shlex.quote(str(wrapper_pid_file)) + '; exec ' +
                       shlex.join([str(BINARY), 'server', '--project', str(self.project)]))
            os.write(descriptor, (shlex.join(['/bin/sh', '-c', command]) + '\n').encode())
            read_until(b'server listening on configured address')
            wrapper_group = int(wrapper_pid_file.read_text())
            child_group = os.tcgetpgrp(descriptor)
            os.write(descriptor, b'\x1a')
            read_until(b'HEXO_TEST_SHELL> ')
            self.assertEqual(os.tcgetpgrp(descriptor), pid)
            os.write(descriptor, b'bg\n')
            read_until(b'HEXO_TEST_SHELL> ')
            self.assertEqual(os.tcgetpgrp(descriptor), pid, 'bg stole the shell terminal')
            os.write(descriptor, b'fg\n')
            deadline = time.monotonic() + 5
            while os.tcgetpgrp(descriptor) != child_group and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertEqual(os.tcgetpgrp(descriptor), child_group)
            os.write(descriptor, b'\x03')
            read_until(b'HEXO_TEST_SHELL> ')
            os.write(descriptor, b'printf "EXIT=%s\\n" "$?"\n')
            read_until(b'EXIT=0\r\n')
        finally:
            for group in (wrapper_group, child_group):
                if group and group != pid:
                    try:
                        os.killpg(group, signal.SIGCONT)
                        os.killpg(group, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
            os.close(descriptor)

    @unittest.skipIf(os.name == 'nt', 'POSIX file descriptors')
    def test_closed_standard_descriptors(self):
        def close_input_and_output():
            os.close(0)
            os.close(1)
        result = self.run_cli('theme', preexec_fn=close_input_and_output)
        self.assertIn('child stdout', result.stderr)
        self.assertIn('child stderr', result.stderr)

    def test_invalid_config_prevents_execution(self):
        import shutil
        executable = self.root / BINARY.name
        shutil.copy2(BINARY, executable)
        (self.root / 'config.yaml').write_text('additionalTools: wrong\n', encoding='utf-8')
        result = subprocess.run([str(executable), 'deploy', '--project', str(self.project)],
                                env=self.env, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(self.calls.exists())

    def test_missing_option_value_prevents_execution(self):
        self.run_cli('deploy', '--log-file', '--dry-run', code=1)
        self.assertFalse(self.calls.exists())

    def test_dry_run_has_no_commands_or_log(self):
        self.run_cli('deploy', '--dry-run', '--log-file', 'dry.log')
        self.assertFalse(self.calls.exists())
        self.assertFalse((self.root / 'dry.log').exists())


if __name__ == '__main__':
    unittest.main()
