/*
 * \brief  Low-level modem power control
 * \author Norman Feske
 * \author Sebastian Sumpf
 * \date   2022-06-15
 *
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _POWER_H_
#define _POWER_H_

/* Genode includes */
#include <pin_control_session/connection.h>
#include <pin_state_session/connection.h>

/* local includes */
#include <types.h>

namespace Modem { struct Power; }


struct Modem::Power
{
	enum class Requested { DONT_CARE, OFF, ON };

	Requested _requested = Requested::DONT_CARE;

	enum class State { UNKNOWN, OFF, STARTING_UP, ON, SHUTTING_DOWN };

	State _state = State::UNKNOWN;

	unsigned _startup_seconds  = 0;
	unsigned _shutdown_seconds = 0;

	Env &_env;

	Delayer &_delayer;

	Pin_state::Connection   _pin_status     { _env, "status"     };

	Pin_control::Connection _pin_battery    { _env, "battery"    },
	                        _pin_dtr        { _env, "dtr"        },
	                        _pin_enable     { _env, "enable"     },
	                        _pin_host_ready { _env, "host-ready" },
	                        _pin_pwrkey     { _env, "pwrkey"     },
	                        _pin_reset      { _env, "reset"      };

	void _update_state_from_pin_status()
	{
		_state = _pin_status.state() ? State::OFF : State::ON;
	}

	void _drive_power_up()
	{
		if (_state == State::UNKNOWN)
			_update_state_from_pin_status();

		switch (_state) {

		case State::OFF:

			/* issue power key pulse >= 500ms */
			log("Powering up modem ...");
			_pin_pwrkey.state(true);
			_delayer.msleep(1000);
			_pin_pwrkey.state(false);

			_startup_seconds = 0;
			_state = State::STARTING_UP;
			break;

		case State::STARTING_UP:
			_startup_seconds++;
			if (_pin_status.state() == 0)
				_state = State::ON;
			break;

		case State::UNKNOWN:
		case State::ON:
		case State::SHUTTING_DOWN:
			break;
		}
	}

	void _drive_power_down()
	{
		if (_state == State::UNKNOWN)
			_update_state_from_pin_status();

		switch (_state) {

		case State::UNKNOWN:
		case State::OFF:
			break;

		case State::STARTING_UP:
		case State::ON:
			_pin_reset.state(true);
			_pin_enable.state(true);

			log("Powering down modem ...");
			_pin_pwrkey.state(true);
			_delayer.msleep(1000);
			_pin_pwrkey.state(false);

			shutdown_triggered();
			break;

		case State::SHUTTING_DOWN:
			_shutdown_seconds++;
			if (_pin_status.state() == 1)
				_state = State::OFF;
			break;
		}
	}

	Power(Env &env, Delayer &delayer) : _env(env), _delayer(delayer)
	{
		/*
		 * Note that by enabling '_pin_battery', the '_pin_status' changes
		 * from 0 (on) to 1 (off). This is not desired in cases where the
		 * modem should keep its state (e.g., PIN) across reboots.
		 *
		 * Open question: How to reliably establish the command channel to the
		 * modem when it is already powered?
		 */
		_pin_battery.state(true);

		_delayer.msleep(30);

		_pin_reset.state(false);
		_pin_host_ready.state(false);
		_pin_dtr.state(false);      /* no suspend */
		_pin_enable.state(false);   /* enable RF */

		_delayer.msleep(30);
	}

	void shutdown_triggered()
	{
		_shutdown_seconds = 0;
		_state = State::SHUTTING_DOWN;
	}

	void apply_config(Xml_node const &config)
	{
		if (!config.has_attribute("power")) {
			_requested = Requested::DONT_CARE;
			return;
		}

		if (config.attribute_value("power", false))
			_requested = Requested::ON;

		/*
		 * Power down the modem via 'pwrkey' only if the AT protocol is
		 * disabled.
		 */
		bool const power_down_without_at_protocol =
			!config.attribute_value("power", false) &&
			!config.attribute_value("at_protocol", true);

		if (power_down_without_at_protocol)
			_requested = Requested::OFF;
	}

	void drive_state_transitions()
	{
		for (;;) {
			State const orig_state = _state;

			switch (_requested) {
			case Requested::DONT_CARE:                break;
			case Requested::ON:  _drive_power_up();   break;
			case Requested::OFF: _drive_power_down(); break;
			}

			if (orig_state == _state)
				break;
		}
	}

	bool needs_update_each_second() const
	{
		return (_state == State::STARTING_UP) || (_state == State::SHUTTING_DOWN);
	}

	void generate_report(Xml_generator &xml) const
	{
		auto power_value = [] (State state)
		{
			switch (state) {
			case State::UNKNOWN:       return "unknown";
			case State::OFF:           return "off";
			case State::STARTING_UP:   return "starting up";
			case State::ON:            return "on";
			case State::SHUTTING_DOWN: return "shutting down";
			}
			return "";
		};

		xml.attribute("power", power_value(_state));

		if (_state == State::STARTING_UP)
			xml.attribute("startup_seconds", _startup_seconds);

		if (_state == State::SHUTTING_DOWN)
			xml.attribute("shutdown_seconds", _shutdown_seconds);
	}
};

#endif /* _POWER_H_ */
