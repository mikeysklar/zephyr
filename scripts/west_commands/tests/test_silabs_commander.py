# Copyright (c) 2026 Mikey Sklar
#
# SPDX-License-Identifier: Apache-2.0

import argparse
from unittest.mock import patch

import pytest

from runners.silabs_commander import SiLabsCommanderBinaryRunner

TEST_DEVICE = 'SIWG917M111MGTBA'
RC_KERNEL_RPS = '/test/build-dir/zephyr/zephyr.rps'


def require_commander(program):
    assert program == 'commander'


def parse_args(*extra):
    parser = argparse.ArgumentParser(allow_abbrev=False)
    SiLabsCommanderBinaryRunner.add_parser(parser)
    return parser.parse_args(['--device', TEST_DEVICE, *extra])


def isfile_only(*existing):
    '''os.path.isfile side_effect that is True only for the given paths.'''
    return lambda path: path in existing


@patch('runners.core.ZephyrBinaryRunner.require', side_effect=require_commander)
@patch('runners.core.ZephyrBinaryRunner.check_call')
def test_rps_bin_outranks_hex(cc, req, runner_config):
    '''A .rps bin_file wins over a coexisting hex_file — a hex with no boot
    header does not boot on this SoC family, so it must not be picked.'''
    cfg = runner_config._replace(bin_file=RC_KERNEL_RPS)
    with patch(
        'runners.silabs_commander.os.path.isfile',
        side_effect=isfile_only(cfg.hex_file, RC_KERNEL_RPS),
    ):
        runner = SiLabsCommanderBinaryRunner.create(cfg, parse_args())
        runner.run('flash')

    cmd = cc.call_args_list[-1].args[0]
    assert cmd[-1] == RC_KERNEL_RPS
    assert '--binary' not in cmd


@patch('runners.core.ZephyrBinaryRunner.require', side_effect=require_commander)
@patch('runners.core.ZephyrBinaryRunner.check_call')
def test_hex_wins_when_bin_is_not_rps(cc, req, runner_config):
    '''Existing behavior is preserved: a non-.rps bin_file does not outrank
    a coexisting hex_file.'''
    with patch(
        'runners.silabs_commander.os.path.isfile',
        side_effect=isfile_only(runner_config.hex_file, runner_config.bin_file),
    ):
        runner = SiLabsCommanderBinaryRunner.create(runner_config, parse_args())
        runner.run('flash')

    cmd = cc.call_args_list[-1].args[0]
    assert cmd[-1] == runner_config.hex_file


@patch('runners.core.ZephyrBinaryRunner.require', side_effect=require_commander)
@patch('runners.core.ZephyrBinaryRunner.check_call')
def test_plain_bin_falls_back_to_binary_address(cc, req, runner_config):
    '''A non-.rps bin_file with no hex present still flashes via
    --binary --address, as before.'''
    with patch(
        'runners.silabs_commander.os.path.isfile', side_effect=isfile_only(runner_config.bin_file)
    ):
        runner = SiLabsCommanderBinaryRunner.create(runner_config, parse_args())
        runner.run('flash')

    cmd = cc.call_args_list[-1].args[0]
    assert '--binary' in cmd
    assert cmd[-1] == runner_config.bin_file


@patch('runners.core.ZephyrBinaryRunner.require', side_effect=require_commander)
@patch('runners.core.ZephyrBinaryRunner.check_call')
def test_missing_rps_falls_back_to_hex(cc, req, runner_config):
    '''bin_file names a .rps that was never built (e.g. a non-siwx91x board
    reusing this runner) — it must not be selected just because it is named.'''
    cfg = runner_config._replace(bin_file=RC_KERNEL_RPS)
    with patch('runners.silabs_commander.os.path.isfile', side_effect=isfile_only(cfg.hex_file)):
        runner = SiLabsCommanderBinaryRunner.create(cfg, parse_args())
        runner.run('flash')

    cmd = cc.call_args_list[-1].args[0]
    assert cmd[-1] == cfg.hex_file


@patch('runners.core.ZephyrBinaryRunner.require', side_effect=require_commander)
@patch('runners.core.ZephyrBinaryRunner.check_call')
def test_no_artifact_raises(cc, req, runner_config):
    '''Neither hex nor bin present on disk is a hard error, not a silent
    no-op flash.'''
    with patch('runners.silabs_commander.os.path.isfile', return_value=False):
        runner = SiLabsCommanderBinaryRunner.create(runner_config, parse_args())
        with pytest.raises(ValueError):
            runner.run('flash')
