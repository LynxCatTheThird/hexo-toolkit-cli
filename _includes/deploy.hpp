#pragma once

#include <format>
#include <string>
#include <string_view>

#include "configs.hpp"
#include "process.hpp"

// Use the project's Hexo parser, configuration loader and deployer registry. Hexo's CLI can
// return success for an empty deployment or an unknown deployer, so validate before calling it.
inline std::string checkedDeployCommand(const Config &config, std::string_view extraArguments) {
    constexpr std::string_view script = R"JS(
(async () => {
  let hexo, failure;
  try {
    const {createRequire} = require('module');
    const path = require('path');
    const fs = require('fs');
    const projectHexo = createRequire(require.resolve('hexo'));
    const cli = createRequire(projectHexo.resolve('hexo-cli'));
    const args = cli('hexo-util').camelCaseKeys(cli('minimist')(['deploy', ...process.argv.slice(1)],
      {string: ['_', 'p', 'path', 's', 'slug']}));
    if (args.h || args.help) throw new Error('Use HexoTool deploy --help to show help without deploying');
    let base = path.resolve(args.cwd || '.');
    while (!fs.existsSync(path.join(base, 'package.json'))) {
      const parent = path.dirname(base);
      if (parent === base) throw new Error('Hexo project not found');
      base = parent;
    }
    const Hexo = createRequire(path.join(base, 'package.json'))('hexo');
    hexo = new Hexo(base, args);
    await hexo.init();
    args._.shift();
    const entries = [].concat(hexo.config.deploy || []);
    if (entries.length === 0) throw new Error('No deployer configured: set deploy.type');
    const deployers = hexo.extend.deployer.list();
    for (const entry of entries) {
      if (!entry || typeof entry.type !== 'string' || !entry.type.trim())
        throw new Error('No deployer configured: every deploy entry must have a type');
      if (!Object.prototype.hasOwnProperty.call(deployers, entry.type))
        throw new Error('Deployer not found: ' + entry.type);
    }
    await hexo.call('deploy', args);
  } catch (error) {
    failure = error;
    process.exitCode = 2;
    if (!hexo) console.error(error);
  } finally {
    if (hexo) await hexo.exit(failure);
  }
})().catch(error => { console.error(error); process.exitCode = 2; });
)JS";
    // Encode the fixed script to keep quotes, newlines and shell metacharacters out of cmd.exe.
    constexpr std::string_view digits = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(script.size() * 2);
    for (char const character : script) {
        const auto byte = static_cast<unsigned int>(static_cast<unsigned char>(character));
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 15U]);
    }
    const auto evaluator = std::format("eval(Buffer.from('{}','hex').toString())", encoded);
    const auto node = config.packageManager == "yarn"   ? "yarn node"
                      : config.packageManager == "pnpm" ? "pnpm exec node"
                                                        : "node";
    return std::format("{} --eval {} --{}", node, quoteShellArgument(evaluator), extraArguments);
}
