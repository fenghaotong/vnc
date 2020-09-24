// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
// All rights reserved.
//
//-------------------------------------------------------------------------
// This file is part of the TightVNC software.  Please visit our Web site:
//
//                       http://www.tightvnc.com/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

#include "tvnserver/resource.h"
#include "ServerConfigDialog.h"
#include "ConfigDialog.h"
#include "ChangePasswordDialog.h"
#include "server-config-lib/Configurator.h"
#include "util/StringParser.h"
#include "CommonInputValidation.h"
#include "UIDataAccess.h"
#include <limits.h>
#include "RConfig.h"
#include <atlstr.h>
#include <sstream>
#include <fstream>
#include "rapidjson/prettywriter.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include <iostream>

#include "util/StringTable.h"
#include "util/AnsiStringStorage.h"

using namespace rr;
using namespace std;
using namespace rapidjson;

// 判断是否在界面选择密码验证
bool USEAUTHBUTTON = false;
// 判断是否在界面操作设置密码
bool PRIMARYPASSWORDBUTTON = false;  
bool VIEWONLYPASSWORDBUTTON = false;

ServerConfigDialog::ServerConfigDialog()
: BaseDialog(IDD_CONFIG_SERVER_PAGE), m_parentDialog(NULL)
{
}

ServerConfigDialog::~ServerConfigDialog()
{
}

void ServerConfigDialog::setParentDialog(BaseDialog *dialog)
{
  m_parentDialog = dialog;
}

BOOL ServerConfigDialog::onInitDialog()
{
  m_config = Configurator::getInstance()->getServerConfig();
  initControls();
  updateUI();

  return TRUE;
}

BOOL ServerConfigDialog::onNotify(UINT controlID, LPARAM data)
{
  if (controlID == IDC_POLLING_INTERVAL_SPIN) {
    LPNMUPDOWN message = (LPNMUPDOWN)data;
    if (message->hdr.code == UDN_DELTAPOS) {
      onPollingIntervalSpinChangePos(message);
    }
  }
  return TRUE;
}

BOOL ServerConfigDialog::onCommand(UINT controlID, UINT notificationID)
{
  if (notificationID == BN_CLICKED) {
    switch (controlID) {
    case IDC_ACCEPT_RFB_CONNECTIONS:
      onAcceptRfbConnectionsClick();
      break;
    case IDC_ACCEPT_HTTP_CONNECTIONS:
      onAcceptHttpConnectionsClick();
      break;
    case IDC_USE_AUTHENTICATION:
      onAuthenticationClick();
      USEAUTHBUTTON = true;
      break;
    case IDC_PRIMARY_PASSWORD:
      onPrimaryPasswordChange();
      PRIMARYPASSWORDBUTTON = true;
      break;
    case IDC_VIEW_ONLY_PASSWORD:
      onReadOnlyPasswordChange();
      VIEWONLYPASSWORDBUTTON = true;
      break;
    case IDC_UNSET_PRIMARY_PASSWORD_BUTTON:
      onUnsetPrimaryPasswordClick();
      break;
    case IDC_UNSET_READONLY_PASSWORD_BUTTON:
      onUnsetReadOnlyPasswordClick();
      break;
    case IDC_ENABLE_FILE_TRANSFERS:
      onFileTransferCheckBoxClick();
      break;
    case IDC_REMOVE_WALLPAPER:
      onRemoveWallpaperCheckBoxClick();
      break;
    case IDC_BLOCK_LOCAL_INPUT:
      onBlockLocalInputChanged();
      break;
    case IDC_BLOCK_REMOTE_INPUT:
      onBlockLocalInputChanged();
      break;
    case IDC_LOCAL_INPUT_PRIORITY:
      onLocalInputPriorityChanged();
      break;
    case IDC_USE_D3D:
      onUseD3DChanged();
      break;
    case IDC_USE_MIRROR_DRIVER:
      // FIXME: For high quality code is needed to use a function.
      ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
      break;
    case IDC_SHOW_TVNCONTROL_ICON_CHECKBOX:
      onShowTrayIconCheckBoxClick();
      break;
    }
  } else if (notificationID == EN_UPDATE) {
    switch (controlID) {
    case IDC_RFB_PORT:
      onRfbPortUpdate();
      break;
    case IDC_HTTP_PORT:
      onHttpPortUpdate();
      break;
    case IDC_POLLING_INTERVAL:
      onPollingIntervalUpdate();
      break;
    case IDC_LOCAL_INPUT_PRIORITY_TIMEOUT:
      onInactivityTimeoutUpdate();
      break;
    }
  }
  return TRUE;
}

bool ServerConfigDialog::validateInput()
{
  bool commonValidationOk =
    CommonInputValidation::validatePort(&m_rfbPort) &&
    CommonInputValidation::validatePort(&m_httpPort) &&
    CommonInputValidation::validateUINT(
      &m_pollingInterval,
      StringTable::getString(IDS_INVALID_POLLING_INTERVAL)) &&
    CommonInputValidation::validateUINT(
      &m_localInputPriorityTimeout,
      StringTable::getString(IDS_INVALID_INACTIVITY_TIMEOUT));

  if (!commonValidationOk) {
    return false;
  }

  int httpPort, rfbPort;

  UIDataAccess::queryValueAsInt(&m_rfbPort, &rfbPort);
  UIDataAccess::queryValueAsInt(&m_httpPort, &httpPort);

  if (rfbPort == httpPort && m_acceptHttpConnections.isChecked()) {
    CommonInputValidation::notifyValidationError(
      &m_httpPort,
      StringTable::getString(IDS_HTTP_RFB_PORTS_ARE_EQUAL));
    return false;
  }

  unsigned int pollingInterval;

  UIDataAccess::queryValueAsUInt(&m_pollingInterval, &pollingInterval);

  pollingInterval = 30;

  if (pollingInterval < ServerConfig::MINIMAL_POLLING_INTERVAL) {
    CommonInputValidation::notifyValidationError(
      &m_pollingInterval,
      StringTable::getString(IDS_POLL_INTERVAL_TOO_SMALL));
    return false;
  }

  unsigned int inactivityTimeout;

  UIDataAccess::queryValueAsUInt(&m_localInputPriorityTimeout, &inactivityTimeout);
  
  inactivityTimeout = 3;

  if (inactivityTimeout < ServerConfig::MINIMAL_LOCAL_INPUT_PRIORITY_TIMEOUT) {
    CommonInputValidation::notifyValidationError(
      &m_localInputPriorityTimeout,
      StringTable::getString(IDS_INACTIVITY_TIMEOUT_TOO_SMALL));
    return false;
  }

  bool passwordSpecified = m_ppControl->hasPassword() || m_vpControl->hasPassword();

  if (m_acceptRfbConnections.isChecked() &&
      m_useAuthentication.isChecked() &&
      !passwordSpecified) {
    MessageBox(m_ctrlThis.getWindow(),
               StringTable::getString(IDS_SET_PASSWORD_NOTIFICATION),
               StringTable::getString(IDS_CAPTION_BAD_INPUT),
               MB_ICONSTOP | MB_OK);
    return false;
  }

  return true;
}

void ServerConfigDialog::updateUI()
{
  m_rfbPort.setSignedInt(m_config->getRfbPort());
  m_httpPort.setSignedInt(m_config->getHttpPort());
  m_pollingInterval.setUnsignedInt(m_config->getPollingInterval());

  m_enableFileTransfers.check(m_config->isFileTransfersEnabled());
  m_removeWallpaper.check(m_config->isRemovingDesktopWallpaperEnabled());

  m_acceptRfbConnections.check(m_config->isAcceptingRfbConnections());
  m_acceptHttpConnections.check(m_config->isAcceptingHttpConnections());

  if (m_config->hasPrimaryPassword()) {
    UINT8 ppCrypted[8];
    m_config->getPrimaryPassword(ppCrypted);
    m_ppControl->setCryptedPassword((const char *)ppCrypted);
  }

  if (m_config->hasReadOnlyPassword()) {
    UINT8 vpCrypted[8];
    m_config->getReadOnlyPassword(vpCrypted);
    m_vpControl->setCryptedPassword((const char *)vpCrypted);
  }

  m_useAuthentication.check(m_config->isUsingAuthentication());

  m_blockLocalInput.check(m_config->isBlockingLocalInput());
  m_blockRemoteInput.check(m_config->isBlockingRemoteInput());
  m_localInputPriority.check(m_config->isLocalInputPriorityEnabled());
  if (m_config->isLocalInputPriorityEnabled()) {
    m_localInputPriorityTimeout.setEnabled(true);
  }
  m_localInputPriorityTimeout.setUnsignedInt(m_config->getLocalInputPriorityTimeout());

  m_useD3D.check(m_config->getD3DIsAllowed());
  m_useMirrorDriver.check(m_config->getMirrorIsAllowed());

  m_showTrayIcon.check(m_config->getShowTrayIconFlag());

  updateCheckboxesState();
  updateControlDependencies();
}

void getLocalIP(char* buffer, int buflen)
{
    char namebuf[256];

    if (gethostname(namebuf, 256) != 0) {
        strncpy(buffer, "Host name unavailable", buflen);
        return;
    };
    HOSTENT* ph = gethostbyname(namebuf);
    if (!ph) {
        strncpy(buffer, "IP address unavailable", buflen);
        return;
    };

    *buffer = '\0';
    char digtxt[5];
    for (int i = 0; ph->h_addr_list[i]; i++) {
        for (int j = 0; j < ph->h_length; j++) {
            sprintf(digtxt, "%d.", (unsigned char)ph->h_addr_list[i][j]);
            strncat(buffer, digtxt, (buflen - 1) - strlen(buffer));
        }
        buffer[strlen(buffer) - 1] = '\0';
        if (ph->h_addr_list[i + 1] != 0)
            strncat(buffer, ", ", (buflen - 1) - strlen(buffer));
    }
}

void ServerConfigDialog::apply()
{
  StringStorage rfbPortText;
  StringStorage httpPortText;
  // Polling interval string storage
  StringStorage pollingIntervalText;

  m_rfbPort.getText(&rfbPortText);
  m_httpPort.getText(&httpPortText);
  m_pollingInterval.getText(&pollingIntervalText);

  int intVal = 0;
  std::string member, filepath, KeyName;
  std::map<std::string, std::string> result;

  // 获取IP
  // StringStorage statusString;
  // char localAddressString[1024];
  // getLocalIP(localAddressString, 1024);
  // AnsiStringStorage ansiString(localAddressString);
  // ansiString.toStringStorage(&statusString);
  // MessageBoxA(NULL, TCHAR2STRING(statusString.getString()).c_str(), "title", MB_OK | MB_OKCANCEL);

  
  // KeyName = "LocalIP";
  member = "Server";
  filepath = "./config/config.json";
  // 将IP写入Json文件
  // JsonToFile(filepath, KeyName, TCHAR2STRING(statusString.getString()));
  // 读取Json文件
  result = FileToJson(filepath, member);
  string PrimaryPassword = result["PrimaryPassword"];
  string ViewOnlyPassword = result["ViewOnlyPassword"];
  int RfbPort = stoi(result["RfbPort"]);
  int HttpPort = stoi(result["HttpPort"]);
  int PollingInterval = stoi(result["PollingInterval"]);

  bool EnableFileTransfers;
  istringstream(result["PollingInterval"]) >> boolalpha >> EnableFileTransfers;

  bool EnableRemovingDesktopWallpaper;
  istringstream(result["EnableRemovingDesktopWallpaper"]) >> boolalpha >> EnableRemovingDesktopWallpaper;

  bool AcceptRfbConnections;
  istringstream(result["AcceptRfbConnections"]) >> boolalpha >> AcceptRfbConnections;

  bool AcceptHttpConnections;
  istringstream(result["AcceptHttpConnections"]) >> boolalpha >> AcceptHttpConnections;

  bool UseAuthentication;
  istringstream(result["UseAuthentication"]) >> boolalpha >> UseAuthentication;

  bool LocalInputPriority;
  istringstream(result["LocalInputPriority"]) >> boolalpha >> LocalInputPriority;

  int InactivityTimeout = stoi(result["InactivityTimeout"]);

  bool BlockLocalInput;
  istringstream(result["BlockLocalInput"]) >> boolalpha >> BlockLocalInput;

  bool BlockRemoteInput;
  istringstream(result["BlockRemoteInput"]) >> boolalpha >> BlockRemoteInput;

  bool MirrorAllowing;
  istringstream(result["MirrorAllowing"]) >> boolalpha >> MirrorAllowing;

  bool D3DAllowing;
  istringstream(result["D3DAllowing"]) >> boolalpha >> D3DAllowing;

  bool ShowTrayIconFlag;
  istringstream(result["ShowTrayIconFlag"]) >> boolalpha >> ShowTrayIconFlag;

  // 下面的代码设置了server tab页的值
  StringParser::parseInt(rfbPortText.getString(), &intVal);
  
  // MessageBoxA(NULL, to_string(RfbPort).c_str(), "rfbPORT", MB_ICONEXCLAMATION | MB_YESNO);
  m_config->setRfbPort(RfbPort);
  
  StringParser::parseInt(httpPortText.getString(), &intVal);
  // MessageBoxA(NULL, to_string(HttpPort).c_str(), "httpPORT", MB_ICONEXCLAMATION | MB_YESNO);
  m_config->setHttpPort(HttpPort);

  StringParser::parseInt(pollingIntervalText.getString(), &intVal);
  // MessageBoxA(NULL, to_string(PollingInterval).c_str(), "pollingInterval", MB_ICONEXCLAMATION | MB_YESNO);
  m_config->setPollingInterval(PollingInterval);

  /*m_config->enableFileTransfers(m_enableFileTransfers.isChecked());
  m_config->enableRemovingDesktopWallpaper(m_removeWallpaper.isChecked());

  m_config->acceptRfbConnections(m_acceptRfbConnections.isChecked());
  m_config->acceptHttpConnections(m_acceptHttpConnections.isChecked());
  m_config->useAuthentication(m_useAuthentication.isChecked());*/
  m_config->enableFileTransfers(EnableFileTransfers);
  m_config->enableRemovingDesktopWallpaper(EnableRemovingDesktopWallpaper);

  m_config->acceptRfbConnections(AcceptRfbConnections);
  m_config->acceptHttpConnections(AcceptHttpConnections);
  // 判断使用配置文件还是界面操作
  if (USEAUTHBUTTON) {
      m_config->useAuthentication(m_useAuthentication.isChecked());
      USEAUTHBUTTON = false;
  }
  else {
      m_config->useAuthentication(UseAuthentication);
  }
  
  // 

  m_config->setLocalInputPriority(LocalInputPriority);
  m_config->setLocalInputPriorityTimeout(InactivityTimeout);

  m_config->blockLocalInput(BlockLocalInput);
  m_config->blockRemoteInput(BlockRemoteInput);

  m_config->setMirrorAllowing(MirrorAllowing);
  m_config->setD3DAllowing(D3DAllowing);
  m_config->setShowTrayIconFlag(ShowTrayIconFlag);

  // 改变密码
  if (!PRIMARYPASSWORDBUTTON) {
      TCHAR* primaryPassword = new TCHAR[PrimaryPassword.size() + 1];
      primaryPassword[PrimaryPassword.size()] = 0;
      std::copy(PrimaryPassword.begin(), PrimaryPassword.end(), primaryPassword);
      // MessageBox(NULL, primaryPassword, L"PrimaryPassword", MB_OK | MB_OKCANCEL);
      PrimaryPasswordChange(primaryPassword);
      delete[] primaryPassword;
  } else {
      PRIMARYPASSWORDBUTTON = false;
  }
  
  if (!VIEWONLYPASSWORDBUTTON) {
      TCHAR* viewonlyPassword = new TCHAR[ViewOnlyPassword.size() + 1];
      viewonlyPassword[ViewOnlyPassword.size()] = 0;
      std::copy(ViewOnlyPassword.begin(), ViewOnlyPassword.end(), viewonlyPassword);
      // MessageBox(NULL, viewonlyPassword, L"ViewonlyPassword", MB_OK | MB_OKCANCEL);
      ReadOnlyPasswordChange(viewonlyPassword);
      delete[] viewonlyPassword;
  } else {
      VIEWONLYPASSWORDBUTTON = false;
  }

  //
  // Primary password.
  //

  if (m_ppControl->hasPassword()) {
    m_config->setPrimaryPassword((const unsigned char *)m_ppControl->getCryptedPassword());
  } else {
    m_config->deletePrimaryPassword();
  }

  //
  // View only password.
  //

  if (m_vpControl->hasPassword()) {
    m_config->setReadOnlyPassword((const unsigned char *)m_vpControl->getCryptedPassword());
  } else {
    m_config->deleteReadOnlyPassword();
  }

  // Local input priority timeout string storage
  /*StringStorage liptStringStorage;
  m_localInputPriorityTimeout.getText(&liptStringStorage);
  int timeout = 0;
  m_config->setLocalInputPriority(m_localInputPriority.isChecked());
  if (StringParser::parseInt(liptStringStorage.getString(), &timeout)) {
    timeout = max(0, timeout);
    m_config->setLocalInputPriorityTimeout((unsigned int)timeout);
  }

  m_config->blockLocalInput(m_blockLocalInput.isChecked());
  m_config->blockRemoteInput(m_blockRemoteInput.isChecked());

  m_config->setMirrorAllowing(m_useMirrorDriver.isChecked());
  m_config->setD3DAllowing(m_useD3D.isChecked());
  m_config->setShowTrayIconFlag(m_showTrayIcon.isChecked());*/
}

void ServerConfigDialog::initControls()
{
  HWND hwnd = m_ctrlThis.getWindow();
  m_rfbPort.setWindow(GetDlgItem(hwnd, IDC_RFB_PORT));
  m_httpPort.setWindow(GetDlgItem(hwnd, IDC_HTTP_PORT));
  m_pollingInterval.setWindow(GetDlgItem(hwnd, IDC_POLLING_INTERVAL));
  m_useD3D.setWindow(GetDlgItem(hwnd, IDC_USE_D3D));
  m_useMirrorDriver.setWindow(GetDlgItem(hwnd, IDC_USE_MIRROR_DRIVER));
  m_enableFileTransfers.setWindow(GetDlgItem(hwnd, IDC_ENABLE_FILE_TRANSFERS));
  m_removeWallpaper.setWindow(GetDlgItem(hwnd, IDC_REMOVE_WALLPAPER));
  m_acceptRfbConnections.setWindow(GetDlgItem(hwnd, IDC_ACCEPT_RFB_CONNECTIONS));
  m_acceptHttpConnections.setWindow(GetDlgItem(hwnd, IDC_ACCEPT_HTTP_CONNECTIONS));
  m_primaryPassword.setWindow(GetDlgItem(hwnd, IDC_PRIMARY_PASSWORD));
  // m_primary.setWindow(GetDlgItem(hwnd, IDC_PRIMARY));
  m_readOnlyPassword.setWindow(GetDlgItem(hwnd, IDC_VIEW_ONLY_PASSWORD));
  m_useAuthentication.setWindow(GetDlgItem(hwnd, IDC_USE_AUTHENTICATION));
  m_unsetPrimaryPassword.setWindow(GetDlgItem(hwnd, IDC_UNSET_PRIMARY_PASSWORD_BUTTON));
  m_unsetReadOnlyPassword.setWindow(GetDlgItem(hwnd, IDC_UNSET_READONLY_PASSWORD_BUTTON));
  m_showTrayIcon.setWindow(GetDlgItem(hwnd, IDC_SHOW_TVNCONTROL_ICON_CHECKBOX));

  m_rfbPortSpin.setWindow(GetDlgItem(hwnd, IDC_RFB_PORT_SPIN));
  m_httpPortSpin.setWindow(GetDlgItem(hwnd, IDC_HTTP_PORT_SPIN));
  m_pollingIntervalSpin.setWindow(GetDlgItem(hwnd, IDC_POLLING_INTERVAL_SPIN));

  m_rfbPortSpin.setBuddy(&m_rfbPort);
  m_rfbPortSpin.setAccel(0, 1);
  m_rfbPortSpin.setRange32(1, 65535);

  m_httpPortSpin.setBuddy(&m_httpPort);
  m_httpPortSpin.setAccel(0, 1);
  m_httpPortSpin.setRange32(1, 65535);

  int limitersTmp[] = {50, 200};
  int deltasTmp[] = {5, 10};

  std::vector<int> limitters(limitersTmp, limitersTmp + sizeof(limitersTmp) /
                                                        sizeof(int));
  std::vector<int> deltas(deltasTmp, deltasTmp + sizeof(deltasTmp) /
                                                 sizeof(int));

  m_pollingIntervalSpin.setBuddy(&m_pollingInterval);
  m_pollingIntervalSpin.setAccel(0, 1);
  m_pollingIntervalSpin.setRange32(1, INT_MAX);
  m_pollingIntervalSpin.setAutoAccelerationParams(&limitters, &deltas, 50);
  m_pollingIntervalSpin.enableAutoAcceleration(true);

  m_blockLocalInput.setWindow(GetDlgItem(hwnd, IDC_BLOCK_LOCAL_INPUT));
  m_blockRemoteInput.setWindow(GetDlgItem(hwnd, IDC_BLOCK_REMOTE_INPUT));
  m_localInputPriority.setWindow(GetDlgItem(hwnd, IDC_LOCAL_INPUT_PRIORITY));
  m_localInputPriorityTimeout.setWindow(GetDlgItem(hwnd, IDC_LOCAL_INPUT_PRIORITY_TIMEOUT));
  m_inactivityTimeoutSpin.setWindow(GetDlgItem(hwnd, IDC_INACTIVITY_TIMEOUT_SPIN));

  m_inactivityTimeoutSpin.setBuddy(&m_localInputPriorityTimeout);
  m_inactivityTimeoutSpin.setAccel(0, 1);
  m_inactivityTimeoutSpin.setRange32(0, INT_MAX);

  m_ppControl = new PasswordControl(&m_primaryPassword, &m_unsetPrimaryPassword);
  m_vpControl = new PasswordControl(&m_readOnlyPassword, &m_unsetReadOnlyPassword);
}

//
// TODO: Add comment to this method
//

void ServerConfigDialog::updateControlDependencies()
{
  // 判断是否接受rfb连接
    std::string member, filepath;
    std::map<std::string, std::string> result;
    member = "Server";
    filepath = "./config/config.json";
    result = FileToJson(filepath, member);
    bool AcceptRfbConnections;
    istringstream(result["AcceptRfbConnections"]) >> boolalpha >> AcceptRfbConnections;

  if (AcceptRfbConnections) {
    m_rfbPort.setEnabled(true);
    m_acceptHttpConnections.setEnabled(true);
    m_useAuthentication.setEnabled(true);
  } else {
    m_rfbPort.setEnabled(false);
    m_acceptHttpConnections.setEnabled(false);
    m_useAuthentication.setEnabled(false);
  }

  if ((m_acceptHttpConnections.isChecked()) && (m_acceptHttpConnections.isEnabled())) {
    m_httpPort.setEnabled(true);
  } else {
    m_httpPort.setEnabled(false);
  }

  bool passwordsAreEnabled = ((m_useAuthentication.isChecked()) && (m_useAuthentication.isEnabled()));

  m_ppControl->setEnabled(passwordsAreEnabled);
  m_vpControl->setEnabled(passwordsAreEnabled);

  m_rfbPortSpin.invalidate();
  m_httpPortSpin.invalidate();
  m_pollingIntervalSpin.invalidate();
}

void ServerConfigDialog::onAcceptRfbConnectionsClick()
{
  updateControlDependencies();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onAcceptHttpConnectionsClick()
{
  updateControlDependencies();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onAuthenticationClick()
{
  updateControlDependencies();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}


void ServerConfigDialog::onShowTrayIconCheckBoxClick()
{
  bool oldVal = m_config->getShowTrayIconFlag();
  bool newVal = m_showTrayIcon.isChecked();

  if (oldVal != newVal) {
    ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
  }
}

void ServerConfigDialog::PrimaryPasswordChange(const TCHAR *password)
{  
  if (m_ppControl->ChangePasswordModal(&m_ctrlThis, password)) {
    ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
  }
}

void ServerConfigDialog::ReadOnlyPasswordChange(const TCHAR* password)
{
  if (m_vpControl->ChangePasswordModal(&m_ctrlThis, password)) {
    ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
  }
}

void ServerConfigDialog::onPrimaryPasswordChange()
{
    if (m_ppControl->showChangePasswordModalDialog(&m_ctrlThis)) {
        ((ConfigDialog*)m_parentDialog)->updateApplyButtonState();
    }
}

void ServerConfigDialog::onReadOnlyPasswordChange()
{
    if (m_vpControl->showChangePasswordModalDialog(&m_ctrlThis)) {
        ((ConfigDialog*)m_parentDialog)->updateApplyButtonState();
    }
}

void ServerConfigDialog::onUnsetPrimaryPasswordClick()
{
  m_ppControl->unsetPassword(true, m_ctrlThis.getWindow());

  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onUnsetReadOnlyPasswordClick()
{
  m_vpControl->unsetPassword(true, m_ctrlThis.getWindow());

  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onPollingIntervalSpinChangePos(LPNMUPDOWN message)
{
  m_pollingIntervalSpin.autoAccelerationHandler(message);
}

void ServerConfigDialog::onRfbPortUpdate()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onHttpPortUpdate()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onUrlParamsClick()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onPollingIntervalUpdate()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onFileTransferCheckBoxClick()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onRemoveWallpaperCheckBoxClick()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onGrabTransparentWindowsChanged()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onUseD3DChanged()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onBlockLocalInputChanged()
{
  updateCheckboxesState();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onBlockRemoteInputChanged()
{
  updateCheckboxesState();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onLocalInputPriorityChanged()
{
  updateCheckboxesState();
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::onInactivityTimeoutUpdate()
{
  ((ConfigDialog *)m_parentDialog)->updateApplyButtonState();
}

void ServerConfigDialog::updateCheckboxesState()
{
  if (m_blockLocalInput.isChecked() || m_blockRemoteInput.isChecked()) {
    m_localInputPriority.check(false);
    m_localInputPriority.setEnabled(false);
  } else {
    m_localInputPriority.setEnabled(true);
  }

  if (m_localInputPriority.isChecked() && m_localInputPriority.isEnabled()) {
    m_localInputPriorityTimeout.setEnabled(true);
  } else {
    m_localInputPriorityTimeout.setEnabled(false);
  }
  m_inactivityTimeoutSpin.invalidate();
}

std::map<string, string> ServerConfigDialog::FileToJson(std::string& filepath, std::string& member) {
    std::map<string, string> result;
    ifstream t(filepath.c_str()); // 输入流
    IStreamWrapper isw(t);
    Document doc;
    doc.ParseStream(isw);
    // 获取对象中的数组，也就是对象是一个数组
    assert(doc.HasMember(member.c_str()));
    const Value& infoArray = doc[member.c_str()];


    if (infoArray.IsInt()) {
        result.insert(make_pair(member, to_string(infoArray.GetInt())));
    }
    else if (infoArray.IsString()) {
        result.insert(make_pair(member, infoArray.GetString()));
    }
    else if (infoArray.IsArray()) {
        for (int i = 0; i < infoArray.Size(); i++) {
            const Value& tempInfo = infoArray[i];
            for (rapidjson::Value::ConstMemberIterator iter = tempInfo.MemberBegin(); iter != tempInfo.MemberEnd(); iter++) {
                const char* key = iter->name.GetString();
                const rapidjson::Value& val = iter->value;
                result.insert(make_pair(key, val.GetString()));
            }
        }
    }
    else {
        for (rapidjson::Value::ConstMemberIterator iter = infoArray.MemberBegin(); iter != infoArray.MemberEnd(); iter++) {
            const char* key = iter->name.GetString();
            const rapidjson::Value& val = iter->value;
            result.insert(make_pair(key, val.GetString()));
        }
    }

    if (t.is_open())
        t.close();
    return result;
}

std::string ServerConfigDialog::JsonToFile(string& filepath, string& key, string& LocalIP) {

    // 读取json文件并修改
    ifstream ifs(filepath);
    IStreamWrapper isw(ifs);
    Document doc;
    doc.ParseStream(isw);

    Value& str = doc[key.c_str()];
    str.SetString(LocalIP.c_str(), strlen(LocalIP.c_str()));

    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    std::string jsonStr(buffer.GetString());

    if (ifs.is_open())
        ifs.close();

    ofstream outfile;
    outfile.open(filepath);
    outfile << jsonStr.c_str() << endl;
    outfile.close();
    return  jsonStr.c_str();
}

std::string ServerConfigDialog::TCHAR2STRING(const TCHAR* str)
{
    std::string strstr;
    try{
        int iLen = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
        char* chRtn = new char[iLen * sizeof(char)];
        WideCharToMultiByte(CP_ACP, 0, str, -1, chRtn, iLen, NULL, NULL);
        strstr = chRtn;
    }
    catch (std::exception e){
    }
    return strstr;
}