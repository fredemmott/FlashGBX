# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)

import datetime
from .i18n import __, c__, c___, ___, format_number, format_decimal

class Formatter:
	@classmethod
	def round2(cls, num, decimals=2):
		x = pow(10, decimals)
		return int(num * x) / x

	@classmethod
	def file_size(cls, size, as_int=False, space=" ", short=False, localized=True):
		_translate = __ if localized else (lambda x: x)
		if size == 1:
			if short: return c___("Bytes (short form)", "B", "B", n=1)
			return _translate(" Byte").replace(" ", space)
		elif size < 1024:
			if short: return "{:d}".format(size) + c___("Bytes (short form)", "B", "B", n=size)
			return "{:d}".format(size) + _translate(" Bytes").replace(" ", space)
		elif size < 1024 * 1024:
			val = cls.round2(size / 1024)
			precision = 0 if as_int else 1
			return format_decimal(val, precision=precision, localized=localized) + _translate(" KiB").replace(" ", space)
		else:
			val = cls.round2(size / 1024 / 1024)
			precision = 0 if as_int else 2
			return format_decimal(val, precision=precision, localized=localized) + _translate(" MiB").replace(" ", space)

	@classmethod
	def progress_time_short(cls, sec):
		sec = sec % (24 * 3600)
		hr = sec // 3600
		sec %= 3600
		minute = sec // 60
		sec %= 60
		return "{:02d}:{:02d}:{:02d}".format(int(hr), int(minute), int(sec))

	@classmethod
	def progress_time(cls, seconds, as_float=False, localized=True):
		if not localized:
			t___ = lambda singular, plural, **kwargs: (singular if kwargs.get("n", 1) == 1 else plural).format(**kwargs)
			tc__ = lambda _context, text, **kwargs: text.format(**kwargs)
		else:
			t___ = ___
			tc__ = c__

		if seconds < 0:
			seconds = 0

		days = int(seconds // 86400)
		remaining = seconds % 86400
		hours = int(remaining // 3600)
		remaining = remaining % 3600
		minutes = int(remaining // 60)
		secs = remaining % 60

		components = [days, hours, minutes]
		parts = []
		for i in range(len(components)):
			if components[i] > 0:
				if i == 0:
					parts.append(t___("{days} day", "{days} days", n=components[i], days=format_number(components[i])))
				elif i == 1:
					parts.append(t___("{hours} hour", "{hours} hours", n=components[i], hours=format_number(components[i])))
				elif i == 2:
					parts.append(t___("{minutes} minute", "{minutes} minutes", n=components[i], minutes=format_number(components[i])))

		if (len(parts) == 0) or (int(secs) != 0) or (seconds < 1 and as_float):
			if seconds < 1 and as_float:
				secs_formatted = format_decimal(secs, precision=2)
				n_value = secs
			else:
				secs_int = int(secs)
				secs_formatted = format_number(secs_int)
				n_value = secs_int
			parts.append(t___("{seconds} second", "{seconds} seconds", n=n_value, seconds=secs_formatted))

		separator = tc__("Time duration separator (e.g. 6 minutes, 4 seconds)", ", ")
		return separator.join(parts)

	@classmethod
	def validate_datetime(cls, string, fmt):
		try:
			if string != datetime.datetime.strptime(string, fmt).strftime(fmt):
				raise ValueError
			return True
		except ValueError:
			return False

	@classmethod
	def title(cls, title):
		if title is None: return ""
		return str(title).replace("\r\n", "␤").replace("\n", "␤").replace("\r", "␤")
