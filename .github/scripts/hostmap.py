"""Import this from a source-scraping test that was written against the old direct calls.

The support backends used to call guiMsgBox()/deinit()/libViewActive()/... directly; they now go through
host::ui()/app()/library() (include/host.h). These tests extract function bodies from src/*.cpp and compile
them against mocks named after the OLD functions, so importing this module makes Path.read_text() hand them the
source with the host calls mapped back to the old names. Mocks keep working; the extracted logic is unchanged.
"""
import pathlib
import re

HOST_TO_OLD = {
    'host::ui().msgBox': 'guiMsgBox', 'host::ui().warning': 'guiWarning', 'host::ui().raNotice': 'guiShowRANotice',
    'host::ui().bootStatusStickyCopy': 'guiSetBootStatusStickyCopy', 'host::ui().bootStatusSticky': 'guiSetBootStatusSticky',
    'host::ui().renderTextScreen': 'guiRenderTextScreen', 'host::ui().manageCheats': 'guiManageCheats',
    'host::ui().showVcdUsbMode': 'guiShowVcdUsbMode', 'host::ui().promptRebootIop': 'guiPromptRebootIop',
    'host::ui().drawBackgroundPlasma': 'guiDrawBGPlasma', 'host::ui().lock': 'guiLock', 'host::ui().unlock': 'guiUnlock',
    'host::app().deinitEx': 'deinitEx', 'host::app().deinit': 'deinit', 'host::app().miniDeinit': 'miniDeinit',
    'host::app().saveConfig': 'saveConfig', 'host::app().applyConfig': 'applyConfig', 'host::app().initSupport': 'initSupport',
    'host::app().moduleUpdateMenu': 'moduleUpdateMenu', 'host::app().pathToMode': 'oplPath2Mode',
    'host::app().setErrorDetail': 'setErrorMessageWithCodeAndDetail', 'host::app().setError': 'setErrorMessageWithCode',
    'host::app().setErrorPath': 'setErrorMessagePathCode', 'host::app().clearErrorIf': 'clearErrorMessageIf',
    'host::app().legacyAppsConfig': 'oplGetLegacyAppsConfig', 'host::app().legacyAppsInfo': 'oplGetLegacyAppsInfo',
    'host::app().scanApps': 'oplScanApps', 'host::app().shouldAppsUpdate': 'oplShouldAppsUpdate',
    'host::library().listViewActive': 'libListViewActive', 'host::library().viewActive': 'libViewActive',
    'host::library().consumeDirty': 'libViewConsumeDirty', 'host::library().markDirty': 'libViewMarkDirty',
    'host::library().listRowView': 'libListRowView', 'host::library().listSourceId': 'libListSourceId',
    'host::library().bdmAttach': 'libViewBdmAttach',
}
_RX = re.compile('|'.join(re.escape(k) for k in sorted(HOST_TO_OLD, key=len, reverse=True)))


_WARN_ID = re.compile(r'host::ui\(\)\.warningId\(\s*(_STR_\w+)\s*,')


def unhost(text):
    # warningId(_STR_X, n) was guiWarning(_l(_STR_X), n)
    text = _WARN_ID.sub(lambda m: 'guiWarning(_l(%s),' % m.group(1), text)
    return _RX.sub(lambda m: HOST_TO_OLD[m.group(0)], text)


_orig_read_text = pathlib.Path.read_text


def _read_text(self, *args, **kwargs):
    text = _orig_read_text(self, *args, **kwargs)
    parts = self.parts
    if self.suffix == '.cpp' and 'src' in parts and self.name != 'host.cpp':
        return unhost(text)
    return text


pathlib.Path.read_text = _read_text
