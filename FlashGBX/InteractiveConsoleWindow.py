# -*- coding: utf-8 -*-
# FlashGBX
# Author: Lesserkuma (github.com/Lesserkuma)

from .pyside import QtCore, QtWidgets, QtGui, QDesktopWidget
from .i18n import __, c__
from .InteractiveConsole import InteractiveConsole
from .app import AppInfo

class InteractiveConsoleWindow(QtWidgets.QDialog):
	APP = None
	CONN = None
	MODE = None
	IM = None

	def __init__(self, app, icon=None):
		QtWidgets.QDialog.__init__(self, app)
		if icon is not None: self.setWindowIcon(QtGui.QIcon(icon))
		self.setWindowTitle(AppInfo.NAME + " – " + __("Interactive Console"))
		flags = self.windowFlags()
		flags = (flags & ~QtCore.Qt.WindowContextHelpButtonHint) | QtCore.Qt.WindowCloseButtonHint | QtCore.Qt.WindowMaximizeButtonHint
		self.setWindowFlags(flags)
		self.resize(700, 400)

		self.APP = app
		self.CONN = app.CONN
		self.MODE = self.CONN.GetMode()
		self.IM = InteractiveConsole(self.CONN, on_output=self.AppendOutput, on_error=self.AppendOutput)

		self.layout = QtWidgets.QVBoxLayout()
		self.layout.setContentsMargins(8, 8, 8, 8)

		mono_font = QtGui.QFontDatabase.systemFont(QtGui.QFontDatabase.FixedFont)
		mono_font.setStyleHint(QtGui.QFont.TypeWriter)

		self.txtOutput = QtWidgets.QPlainTextEdit()
		self.txtOutput.setReadOnly(True)
		self.txtOutput.setFont(mono_font)
		self.txtOutput.setLineWrapMode(QtWidgets.QPlainTextEdit.NoWrap)
		self.layout.addWidget(self.txtOutput)

		rowInput = QtWidgets.QHBoxLayout()
		self.lblPrompt = QtWidgets.QLabel(">")
		self.lblPrompt.setFont(mono_font)
		self.txtInput = QtWidgets.QLineEdit()
		self.txtInput.setFont(mono_font)
		self.txtInput.returnPressed.connect(self.OnSubmit)
		rowInput.addWidget(self.lblPrompt)
		rowInput.addWidget(self.txtInput)
		self.layout.addLayout(rowInput)

		rowButtons = QtWidgets.QHBoxLayout()
		rowButtons.addStretch()
		self.btnClose = QtWidgets.QPushButton(c__("Button (& = Keyboard Shortcut)", "&Close"))
		self.btnClose.setStyleSheet("padding: 5px 15px;")
		self.btnClose.setAutoDefault(False)
		self.btnClose.setDefault(False)
		self.btnClose.clicked.connect(self.reject)
		rowButtons.addWidget(self.btnClose)
		self.layout.addLayout(rowButtons)

		self.setLayout(self.layout)

		self.History = []
		self.HistoryIndex = 0
		self.txtInput.installEventFilter(self)

	def run(self):
		self.CONN.SetAutoPowerOff(value=0)
		self.CONN.CartPowerOn()
		self.IM.print_help()
		self.txtInput.setFocus()
		self.layout.update()
		self.layout.activate()
		screenGeometry = QDesktopWidget().screenGeometry(self)
		x = (screenGeometry.width() - self.width()) / 2
		y = (screenGeometry.height() - self.height()) / 2
		self.move(x, y)
		self.show()

	def hideEvent(self, event):
		try:
			self.APP.SetAutoPowerOff()
		except:
			pass
		self.APP.activateWindow()

	def eventFilter(self, obj, event):
		if obj is self.txtInput and event.type() == QtCore.QEvent.KeyPress:
			if event.key() == QtCore.Qt.Key_Up:
				if self.History and self.HistoryIndex > 0:
					self.HistoryIndex -= 1
					self.txtInput.setText(self.History[self.HistoryIndex])
				return True
			elif event.key() == QtCore.Qt.Key_Down:
				if self.History and self.HistoryIndex < len(self.History) - 1:
					self.HistoryIndex += 1
					self.txtInput.setText(self.History[self.HistoryIndex])
				else:
					self.HistoryIndex = len(self.History)
					self.txtInput.clear()
				return True
		return super().eventFilter(obj, event)

	def AppendOutput(self, text):
		self.txtOutput.appendPlainText(text)
		self.txtOutput.verticalScrollBar().setValue(self.txtOutput.verticalScrollBar().maximum())

	def OnSubmit(self):
		line = self.txtInput.text().strip()
		self.txtInput.clear()
		if not line:
			self.AppendOutput("")
			return
		self.AppendOutput("> " + line)
		if not self.History or self.History[-1] != line:
			self.History.append(line)
		self.HistoryIndex = len(self.History)

		self.txtInput.setEnabled(False)
		self.btnClose.setEnabled(False)
		try:
			if not self.IM.execute_line(line):
				self.reject()
				return
		finally:
			self.txtInput.setEnabled(True)
			self.btnClose.setEnabled(True)
			self.txtInput.setFocus()
