#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include "FrameDebugger.h"
#include "win32/AcceleratorTableGenerator.h"
#include "win32/Rect.h"
#include "win32/HorizontalSplitter.h"
#include "win32/FileDialog.h"
#include "win32/MenuItem.h"
#include "../resource.h"
#include "StdStreamUtils.h"
#include "lexical_cast_ex.h"
#include "string_cast.h"
#include "string_format.h"

#define WNDSTYLE					(WS_CLIPCHILDREN | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX)
#define WNDTITLE					_T("Play! - Frame Debugger")

CFrameDebugger::CFrameDebugger()
: m_accTable(nullptr)
{
	Create(0, Framework::Win32::CDefaultWndClass::GetName(), WNDTITLE, WNDSTYLE, Framework::Win32::CRect(0, 0, 1024, 768), nullptr, nullptr);
	SetClassPtr();

	m_handlerOutputWindow = std::make_unique<COutputWnd>(m_hWnd);
	m_handlerOutputWindow->Show(SW_SHOW);

	m_gs = std::make_unique<CGSH_Direct3D9>(m_handlerOutputWindow.get());
	m_gs->SetLoggingEnabled(false);
	m_gs->Initialize();
	m_gs->Reset();

	memset(&m_currentMetadata, 0, sizeof(m_currentMetadata));

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_FRAMEDEBUGGER)));

	m_mainSplitter = std::make_unique<Framework::Win32::CHorizontalSplitter>(m_hWnd, GetClientRect());

	m_registerWriteListView = std::make_unique<CGsRegisterWriteListView>(*m_mainSplitter, GetClientRect());

	m_tab = std::make_unique<CTabHost>(*m_mainSplitter, GetClientRect());

	m_gsContextView0 = std::make_unique<CGsContextView>(*m_tab, GetClientRect(), m_gs.get(), 0);
	m_gsContextView0->Show(SW_HIDE);

	m_gsContextView1 = std::make_unique<CGsContextView>(*m_tab, GetClientRect(), m_gs.get(), 1);
	m_gsContextView1->Show(SW_HIDE);

	m_gsInputStateView = std::make_unique<CGsInputStateView>(*m_tab, GetClientRect());

	m_vu1ProgramView = std::make_unique<CVu1ProgramView>(*m_tab, GetClientRect(), m_vu1vm);
	m_vu1ProgramView->Show(SW_HIDE);

	m_tab->InsertTab(_T("Context 1"), m_gsContextView0.get());
	m_tab->InsertTab(_T("Context 2"), m_gsContextView1.get());
	m_tab->InsertTab(_T("Input State"), m_gsInputStateView.get());
	m_tab->InsertTab(_T("VU1 Microprogram"), m_vu1ProgramView.get());

	UpdateMenus();

	m_mainSplitter->SetChild(0, *m_registerWriteListView);
	m_mainSplitter->SetChild(1, *m_tab);

	CreateAcceleratorTables();
}

CFrameDebugger::~CFrameDebugger()
{
	m_gs->Release();

	if(m_accTable != NULL)
	{
		DestroyAcceleratorTable(m_accTable);
	}
}

HACCEL CFrameDebugger::GetAccelerators()
{
	return m_accTable;
}

long CFrameDebugger::OnSize(unsigned int type, unsigned int x, unsigned int y)
{
	if(m_mainSplitter)
	{
		Framework::Win32::CRect splitterRect = GetClientRect();
		splitterRect.Inflate(-10, -10);
		m_mainSplitter->SetSizePosition(splitterRect);
	}
	return TRUE;
}

long CFrameDebugger::OnCommand(unsigned short id, unsigned short msg, HWND hwndFrom)
{
	if(!IsWindowEnabled(m_hWnd))
	{
		return TRUE;
	}

	if(hwndFrom == NULL)
	{
		switch(id)
		{
		case ID_FD_FILE_LOADDUMP:
			ShowFrameDumpSelector();
			break;
		case ID_FD_SETTINGS_ALPHATEST:
			ToggleAlphaTest();
			break;
		case ID_FD_SETTINGS_DEPTHTEST:
			ToggleDepthTest();
			break;
		case ID_FD_SETTINGS_ALPHABLEND:
			ToggleAlphaBlending();
			break;
		case ID_FD_VU1_STEP:
			StepVu1();
			break;
		}
	}
	return TRUE;
}

long CFrameDebugger::OnNotify(WPARAM param, NMHDR* header)
{
	if(!IsWindowEnabled(m_hWnd))
	{
		return FALSE;
	}

	if(CWindow::IsNotifySource(m_tab.get(), header))
	{
		switch(header->code)
		{
		case CTabHost::NOTIFICATION_SELCHANGED:
			UpdateCurrentTab();
			break;
		}
		return FALSE;
	}
	else if(CWindow::IsNotifySource(m_registerWriteListView.get(), header))
	{
		switch(header->code)
		{
		case CGsRegisterWriteListView::NOTIFICATION_SELCHANGED:
			{
				auto selchangedInfo = reinterpret_cast<CGsRegisterWriteListView::SELCHANGED_INFO*>(header);
				UpdateDisplay(selchangedInfo->selectedCmdIndex);
			}
			break;
		}
		return FALSE;
	}
	return FALSE;
}

long CFrameDebugger::OnSysCommand(unsigned int nCmd, LPARAM lParam)
{
	switch(nCmd)
	{
	case SC_CLOSE:
		Show(SW_HIDE);
		return FALSE;
	case SC_KEYMENU:
		return FALSE;
	}
	return TRUE;
}

void CFrameDebugger::CreateAcceleratorTables()
{
	Framework::Win32::CAcceleratorTableGenerator generator;
	generator.Insert(ID_FD_VU1_STEP,			VK_F10, FVIRTKEY);
	m_accTable = generator.Create();
}

void CFrameDebugger::UpdateMenus()
{
	{
		auto alphaTestMenuItem = Framework::Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_FD_SETTINGS_ALPHATEST);
		assert(!alphaTestMenuItem.IsEmpty());
		alphaTestMenuItem.Check(m_gs->GetAlphaTestingEnabled());
	}
	{
		auto depthTestMenuItem = Framework::Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_FD_SETTINGS_DEPTHTEST);
		assert(!depthTestMenuItem.IsEmpty());
		depthTestMenuItem.Check(m_gs->GetDepthTestingEnabled());
	}
	{
		auto alphaBlendMenuItem = Framework::Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_FD_SETTINGS_ALPHABLEND);
		assert(!alphaBlendMenuItem.IsEmpty());
		alphaBlendMenuItem.Check(m_gs->GetAlphaBlendingEnabled());
	}
}

void CFrameDebugger::UpdateDisplay(int32 targetCmdIndex)
{
	EnableWindow(m_hWnd, FALSE);

	m_gs->Reset();

	uint8* gsRam = m_gs->GetRam();
	uint64* gsRegisters = m_gs->GetRegisters();
	memcpy(gsRam, m_frameDump.GetInitialGsRam(), CGSHandler::RAMSIZE);
	memcpy(gsRegisters, m_frameDump.GetInitialGsRegisters(), CGSHandler::REGISTER_MAX * sizeof(uint64));
	m_gs->SetSMODE2(m_frameDump.GetInitialSMODE2());

	CGsPacket::WriteArray writes;

	int32 cmdIndex = 0;
	for(const auto& packet : m_frameDump.GetPackets())
	{
		if((cmdIndex - 1) >= targetCmdIndex) break;

		m_currentMetadata = packet.metadata;

		for(const auto& registerWrite : packet.writes)
		{
			if((cmdIndex - 1) >= targetCmdIndex) break;
			writes.push_back(registerWrite);
			cmdIndex++;
		}
	}

	m_gs->WriteRegisterMassively(writes.data(), writes.size(), nullptr);
	m_gs->Flip();

	const auto& drawingKicks = m_frameDump.GetDrawingKicks();
	auto prevKickIndexIterator = drawingKicks.lower_bound(targetCmdIndex);
	if((prevKickIndexIterator == std::end(drawingKicks)) || (prevKickIndexIterator->first != targetCmdIndex))
	{
		prevKickIndexIterator = std::prev(prevKickIndexIterator);
	}
	if(prevKickIndexIterator != std::end(drawingKicks))
	{
		m_currentDrawingKick = prevKickIndexIterator->second;
	}
	else
	{
		m_currentDrawingKick = DRAWINGKICK_INFO();
	}
	UpdateCurrentTab();

	EnableWindow(m_hWnd, TRUE);
}

void CFrameDebugger::UpdateCurrentTab()
{
	if(m_tab->GetSelection() != -1)
	{
		if(auto tab = m_tab->GetTab(m_tab->GetSelection()))
		{
			auto debuggerTab = dynamic_cast<IFrameDebuggerTab*>(tab);
			debuggerTab->UpdateState(m_gs.get(), &m_currentMetadata, &m_currentDrawingKick);
		}
	}
}

void CFrameDebugger::LoadFrameDump(const TCHAR* dumpPathName)
{
	try
	{
		boost::filesystem::path dumpPath(dumpPathName);
		auto inputStream = Framework::CreateInputStdStream(dumpPath.native());
		m_frameDump.Read(inputStream);
		m_frameDump.IdentifyDrawingKicks();
	}
	catch(const std::exception& exception)
	{
		std::string message = string_format("Failed to open frame dump:\r\n\r\n%s", exception.what());
		MessageBox(m_hWnd, string_cast<std::tstring>(message).c_str(), nullptr, MB_ICONERROR);
		return;
	}

	m_vu1vm.Reset();
	m_registerWriteListView->SetFrameDump(&m_frameDump);

	UpdateDisplay(0);
}

void CFrameDebugger::ShowFrameDumpSelector()
{
	Framework::Win32::CFileDialog fileDialog;
	fileDialog.m_OFN.lpstrFilter = _T("Play! Frame Dumps (*.dmp.zip)\0*.dmp.zip\0All files (*.*)\0*.*\0");
	unsigned int result = fileDialog.SummonOpen(m_hWnd);
	if(result == IDOK)
	{
		LoadFrameDump(fileDialog.GetPath());
	}
}

void CFrameDebugger::ToggleAlphaTest()
{
	m_gs->SetAlphaTestingEnabled(!m_gs->GetAlphaTestingEnabled());
	uint32 selectedItemIndex = m_registerWriteListView->GetSelectedItemIndex();
	if(selectedItemIndex != -1)
	{
		UpdateDisplay(selectedItemIndex);
	}
	UpdateMenus();
}

void CFrameDebugger::ToggleDepthTest()
{
	m_gs->SetDepthTestingEnabled(!m_gs->GetDepthTestingEnabled());
	uint32 selectedItemIndex = m_registerWriteListView->GetSelectedItemIndex();
	if(selectedItemIndex != -1)
	{
		UpdateDisplay(selectedItemIndex);
	}
	UpdateMenus();
}

void CFrameDebugger::ToggleAlphaBlending()
{
	m_gs->SetAlphaBlendingEnabled(!m_gs->GetAlphaBlendingEnabled());
	uint32 selectedItemIndex = m_registerWriteListView->GetSelectedItemIndex();
	if(selectedItemIndex != -1)
	{
		UpdateDisplay(selectedItemIndex);
	}
	UpdateMenus();
}

void CFrameDebugger::StepVu1()
{
#ifdef DEBUGGER_INCLUDED
	if(m_currentMetadata.pathIndex != 1)
	{
		MessageBeep(-1);
		return;
	}

	const int vu1TabIndex = 3;
	if(m_tab->GetSelection() != vu1TabIndex)
	{
		m_tab->SetSelection(vu1TabIndex);
	}

	m_vu1ProgramView->StepVu1();
#endif
}
