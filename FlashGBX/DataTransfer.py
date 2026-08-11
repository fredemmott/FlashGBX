# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)

import traceback
from serial import SerialException
from . import pyside
from .Logging import dprint
from .i18n import __

class DataTransfer(pyside.QtCore.QThread):
	CONFIG = None
	FINISHED = False

	updateProgress = pyside.QtCore.Signal(object)

	def __init__(self, config=None):
		pyside.QtCore.QThread.__init__(self)
		if config is not None:
			self.CONFIG = config
		self.FINISHED = False

	def setConfig(self, config):
		self.CONFIG = config
		self.FINISHED = False

	def isRunning(self):
		return not self.FINISHED

	def run(self):
		tb = ""
		error = None
		try:
			if self.CONFIG is None:
				self.FINISHED = True
				return
			else:
				self.FINISHED = False
				self.CONFIG['port'].TransferData(self.CONFIG, self.updateProgress)
				self.FINISHED = True

		except SerialException as e:
			if e.args and isinstance(e.args[0], str) and "GetOverlappedResult failed" in e.args[0]:
				self.updateProgress.emit({
					"action": "ABORT",
					"info_type": "msgbox_critical",
					"info_msg": __("The USB connection was lost during a transfer. Try different USB cables, reconnect the device, restart the software and try again."),
					"abortable": False
				})
				self.FINISHED = True
				return
			tb = traceback.format_exc()
			error = e

		except Exception as e:
			tb = traceback.format_exc()
			error = e

		if error is not None:
			print(tb)
			dprint(tb)
			self.updateProgress.emit({
				"action": "ABORT",
				"info_type": "msgbox_critical",
				"fatal": True,
				"info_msg": __("An unresolvable error has occured. See the debug log file for more information. Reconnect the device, restart the software and try again.")
					+ "\n\n{:s}: {:s}".format(type(error).__name__, str(error)),
				"abortable":False
			})
			self.FINISHED = True
