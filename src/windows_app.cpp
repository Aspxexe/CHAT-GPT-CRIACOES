#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include "aurora/cpu.hpp"
#include "aurora/execution.hpp"
#include "aurora/homebrew.hpp"
#include "aurora/media.hpp"
#include "aurora/ps1_memory.hpp"
#include "aurora/vulkan_presenter.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int importId = 101, demoId = 102, runId = 103, removeId = 104, libraryId = 105,
              diagnosticId = 106, homebrewId = 107;
HWND libraryWindow, statusWindow, videoWindow, window;
HFONT font;
HBRUSH background;
std::filesystem::path dataDirectory;
std::vector<std::filesystem::path> library;
std::unique_ptr<aurora::Ps1Memory> memory;
std::unique_ptr<aurora::Cpu> cpu;
std::unique_ptr<aurora::VulkanPresenter> presenter;
std::vector<std::uint32_t> pixels;
aurora::ExecutionTrace trace;
std::string lastReport;

std::uint16_t keyboardButtons() {
    if (GetForegroundWindow() != window || IsIconic(window)) return 0;
    const auto held = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    std::uint16_t buttons = 0;
    if (held(VK_UP) || held('W')) buttons |= 0x0010;
    if (held(VK_RIGHT) || held('D')) buttons |= 0x0020;
    if (held(VK_DOWN) || held('S')) buttons |= 0x0040;
    if (held(VK_LEFT) || held('A')) buttons |= 0x0080;
    if (held(VK_RETURN) || held('R')) buttons |= 0x0008;
    if (held(VK_SPACE) || held('Z')) buttons |= 0x4000;
    return buttons;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!count) return L"Texto invalido";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), count);
    return result;
}
void status(const std::wstring& value) { SetWindowTextW(statusWindow, value.c_str()); }
bool saveReport(const std::string& content) {
    std::ofstream output(dataDirectory / L"diagnostico.txt", std::ios::binary);
    output << content;
    return bool(output);
}
bool saveReport() { return saveReport(lastReport); }
void clearSession() {
    KillTimer(window, 1);
    cpu.reset();
    memory.reset();
    presenter.reset();
    trace = {};
    pixels.clear();
    InvalidateRect(videoWindow, nullptr, TRUE);
}
void report(const std::exception& error) {
    KillTimer(window, 1);
    if (cpu) lastReport += aurora::executionReport(*cpu, trace);
    lastReport += "\nErro: " + std::string(error.what()) + '\n';
    const bool saved = saveReport();
    clearSession();
    status(L"Execucao interrompida: " + wide(error.what()));
    const auto message = wide(error.what()) +
        L"\n\nUse Copiar ultimo diagnostico para obter os detalhes."
        + (saved ? L"\nRelatorio salvo em: " + (dataDirectory / L"diagnostico.txt").wstring() : L"\nNao foi possivel gravar o relatorio em disco.");
    MessageBoxW(window, message.c_str(), L"Aurora - parada do nucleo experimental", MB_OK | MB_ICONINFORMATION);
}
void copyDiagnostic() {
    const int selected = ListView_GetNextItem(libraryWindow, -1, LVNI_SELECTED);
    std::string content = lastReport;
    if (content.empty() && selected >= 0) {
        content = aurora::diagnoseMedia(library.at(selected));
    } else if (cpu) content += aurora::executionReport(*cpu, trace);
    if (content.empty()) { status(L"Selecione uma midia ou execute a demo para gerar um diagnostico."); return; }
    const auto text = wide(content);
    auto storage = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!storage) throw std::runtime_error("Memoria insuficiente para copiar diagnostico");
    auto contents = GlobalLock(storage);
    if (!contents) { GlobalFree(storage); throw std::runtime_error("Falha ao preparar area de transferencia"); }
    std::memcpy(contents, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(storage);
    if (!OpenClipboard(window)) { GlobalFree(storage); throw std::runtime_error("Area de transferencia ocupada"); }
    const bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, storage);
    CloseClipboard();
    if (!copied) { GlobalFree(storage); throw std::runtime_error("Nao foi possivel copiar o diagnostico"); }
    status(L"Diagnostico da ultima tentativa copiado. O campo Arquivo identifica a midia analisada.");
}
void saveLibrary() {
    const auto temporary = dataDirectory / L"library.tmp";
    const auto destination = dataDirectory / L"library.txt";
    {
        std::ofstream output(temporary, std::ios::binary);
        for (const auto& path : library) output << path.u8string() << '\n';
        if (!output) throw std::runtime_error("Falha ao salvar biblioteca");
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Nao foi possivel substituir a biblioteca salva");
}
void refreshLibrary() {
    ListView_DeleteAllItems(libraryWindow);
    for (std::size_t index = 0; index < library.size(); ++index) {
        auto name = library[index].filename().wstring();
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = int(index);
        item.pszText = name.data();
        ListView_InsertItem(libraryWindow, &item);
        std::wstring kind, state;
        try {
            const auto info = aurora::inspectMedia(library[index]);
            kind = wide(info.kind);
            state = info.executable ? L"Loader PS1 experimental" : L"Sem boot PS1";
        } catch (const std::exception&) {
            kind = L"Indisponivel";
            state = L"Arquivo ausente/invalido";
        }
        ListView_SetItemText(libraryWindow, int(index), 1, kind.data());
        ListView_SetItemText(libraryWindow, int(index), 2, state.data());
    }
}
void addMedia(const std::filesystem::path& input) {
    clearSession();
    lastReport = aurora::diagnoseMedia(input);
    const auto path = std::filesystem::canonical(input);
    lastReport = aurora::diagnoseMedia(path);
    const auto info = aurora::inspectMedia(path);
    const auto existing = std::find_if(library.begin(), library.end(), [&](const auto& candidate) {
        return _wcsicmp(candidate.c_str(), path.c_str()) == 0;
    });
    if (existing == library.end()) {
        library.push_back(path);
        saveLibrary();
        refreshLibrary();
    }
    status(wide(info.description));
}
void startProgram(const std::filesystem::path& path) {
    clearSession();
    lastReport = aurora::diagnoseMedia(path);
    const auto info = aurora::inspectMedia(path);
    if (!info.executable) {
        status(wide(info.description));
        saveReport();
        const auto message = wide(info.description) +
            L"\n\nNao foi encontrado um executavel PS1 carregavel.\n"
            L"Use Copiar ultimo diagnostico para ver o formato, as assinaturas e a causa.";
        MessageBoxW(window, message.c_str(), L"Midia sem boot PS1 disponivel", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const auto program = aurora::loadProgram(path);
    const auto& exe = program.executable;
    lastReport += "\nOrigem do programa: " + program.origin + '\n';
    memory = std::make_unique<aurora::Ps1Memory>();
    memory->load(exe);
    cpu = std::make_unique<aurora::Cpu>(*memory);
    cpu->reset(exe.entry);
    cpu->regs[28] = exe.gp;
    if (exe.stackBase) cpu->regs[29] = cpu->regs[30] = exe.stackBase + exe.stackOffset;
    memory->setPadButtons(0);
    memory->beginFrame();
    aurora::runPs1(*cpu, *memory, 50000, trace);
    if (!cpu->fault.empty()) throw std::runtime_error(cpu->fault);
    if (!presenter) presenter = std::make_unique<aurora::VulkanPresenter>(videoWindow);
    pixels = memory->framebuffer();
    presenter->present(pixels, 320, 240);
    status(L"PS1 | " + wide(presenter->deviceName()) +
           L" | Setas/WASD: mover. Enter/R: reiniciar. Pausa ao sair da janela.");
    saveReport(lastReport + aurora::executionReport(*cpu, trace));
    SetFocus(videoWindow);
    SetTimer(window, 1, 16, nullptr);
}
void chooseMedia() {
    wchar_t path[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrFilter = L"Midias e programas\0*.iso;*.pkg;*.cue;*.bin;*.ps-exe;*.exe\0Todos os arquivos\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Adicionar arquivo - o formato sera inspecionado";
    if (GetOpenFileNameW(&dialog)) addMedia(path);
}
HWND control(const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0) {
    auto handle = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, window, reinterpret_cast<HMENU>(std::intptr_t(id)), GetModuleHandleW(nullptr), nullptr);
    if (!handle) throw std::runtime_error("Falha ao criar controle Windows");
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return handle;
}
void initControls() {
    control(L"STATIC", L"AURORA", SS_LEFT, 24, 25, 155, 36);
    control(L"STATIC", L"BIBLIOTECA\n\nPS1  [experimental]\n\nPLANEJADOS\n\nPS2\nPS3\nPS4\nPSP\nPS Vita\nWii / Wii U\nNew 3DS\nXbox 360 / One", SS_LEFT, 24, 88, 165, 490);
    control(L"STATIC", L"Seu acervo, um ponto de partida", SS_LEFT, 212, 25, 850, 30);
    control(L"STATIC", L"Aurora Coleta | jogo caseiro PS1 integrado, com controles", SS_LEFT, 212, 60, 850, 25);
    control(L"BUTTON", L"Adicionar arquivo", BS_PUSHBUTTON, 212, 102, 170, 36, importId);
    control(L"BUTTON", L"Jogar Aurora Coleta", BS_PUSHBUTTON, 394, 102, 170, 36, homebrewId);
    control(L"BUTTON", L"Executar selecionado", BS_PUSHBUTTON, 576, 102, 195, 36, runId);
    control(L"BUTTON", L"Remover do acervo", BS_PUSHBUTTON, 783, 102, 185, 36, removeId);
    control(L"BUTTON", L"Copiar ultimo diagnostico", BS_PUSHBUTTON, 212, 536, 240, 30, diagnosticId);
    control(L"BUTTON", L"Demo grafica anterior", BS_PUSHBUTTON, 464, 536, 258, 30, demoId);
    libraryWindow = control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                            212, 160, 510, 365, libraryId);
    ListView_SetExtendedListViewStyle(libraryWindow, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(libraryWindow, RGB(26, 34, 47));
    ListView_SetTextBkColor(libraryWindow, RGB(26, 34, 47));
    ListView_SetTextColor(libraryWindow, RGB(229, 237, 248));
    const wchar_t* headings[] = {L"Arquivo", L"Formato", L"Estado"};
    const int widths[] = {205, 105, 190};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(headings[i]);
        column.cx = widths[i];
        ListView_InsertColumn(libraryWindow, i, &column);
    }
    videoWindow = CreateWindowExW(0, L"AuroraVideo", L"", WS_CHILD | WS_VISIBLE,
        744, 160, 384, 288, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!videoWindow) throw std::runtime_error("Falha ao criar janela Vulkan");
    control(L"STATIC", L"AURORA COLETA\n\nColete 5 cristais e evite o bloco vermelho.\n\nSetas / WASD: mover\nEnter / R: reiniciar\n\nPausa ao sair da janela.\nSaida Vulkan: 320 x 240",
            SS_LEFT, 744, 465, 384, 185);
    statusWindow = control(L"STATIC", L"Pronto. Clique em Jogar Aurora Coleta. O jogo proprio ja esta integrado ao programa.",
                            SS_LEFT, 212, 565, 510, 90);
    control(L"STATIC", L"PROTOTIPO 0.3.0 | Homebrew proprio jogavel; jogos comerciais e demais consoles ainda sem suporte.", SS_LEFT,
            212, 680, 920, 30);
    std::ifstream input(dataDirectory / L"library.txt", std::ios::binary);
    std::string line;
    while (library.size() < 10000 && std::getline(input, line)) {
        if (!line.empty() && line.size() <= 131072) library.push_back(std::filesystem::u8path(line));
    }
    refreshLibrary();
}
LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
        switch (message) {
        case WM_COMMAND:
            if (LOWORD(wparam) == importId) chooseMedia();
            else if (LOWORD(wparam) == diagnosticId) copyDiagnostic();
            else if (LOWORD(wparam) == homebrewId) {
                const auto bytes = aurora::makeHomebrewPsxExe();
                const auto path = dataDirectory / L"aurora-coleta.ps-exe";
                {
                    std::ofstream output(path, std::ios::binary);
                    if (!output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
                        throw std::runtime_error("Falha ao preparar Aurora Coleta");
                }
                addMedia(path);
                startProgram(path);
            }
            else if (LOWORD(wparam) == demoId) {
                const auto bytes = aurora::makeDemoPsxExe();
                const auto path = dataDirectory / L"aurora-demo.ps-exe";
                {
                    std::ofstream output(path, std::ios::binary);
                    if (!output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
                        throw std::runtime_error("Falha ao salvar demo local");
                }
                addMedia(path);
                startProgram(path);
            } else if (LOWORD(wparam) == runId || LOWORD(wparam) == removeId) {
                const int selected = ListView_GetNextItem(libraryWindow, -1, LVNI_SELECTED);
                if (selected < 0) { status(L"Selecione um arquivo da biblioteca."); return 0; }
                if (LOWORD(wparam) == runId) startProgram(library.at(selected));
                else { library.erase(library.begin() + selected); saveLibrary(); refreshLibrary(); }
            }
            return 0;
        case WM_TIMER:
            if (presenter && cpu) {
                if (GetForegroundWindow() != window || IsIconic(window)) {
                    memory->setPadButtons(0);
                    return 0;
                }
                memory->setPadButtons(keyboardButtons());
                memory->beginFrame();
                aurora::runPs1(*cpu, *memory, 50000, trace);
                if (!cpu->fault.empty()) throw std::runtime_error(cpu->fault);
                pixels = memory->framebuffer();
                presenter->present(pixels, 320, 240);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(wparam), RGB(215, 227, 244));
            SetBkColor(reinterpret_cast<HDC>(wparam), RGB(15, 23, 36));
            return reinterpret_cast<LRESULT>(background);
        case WM_CLOSE:
            KillTimer(handle, 1);
            presenter.reset();
            DestroyWindow(handle);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) { report(error); return 0; }
    return DefWindowProcW(handle, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&common);
        SetProcessDPIAware();
        PWSTR localAppData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
            throw std::runtime_error("Pasta LocalAppData indisponivel");
        dataDirectory = std::filesystem::path(localAppData) / L"AuroraPrototype";
        CoTaskMemFree(localAppData);
        std::filesystem::create_directories(dataDirectory);
        font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
        background = CreateSolidBrush(RGB(15, 23, 36));
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = procedure;
        type.lpszClassName = L"AuroraLibrary";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = background;
        if (!RegisterClassW(&type)) throw std::runtime_error("Registro de janela falhou");
        WNDCLASSW video{};
        video.hInstance = instance;
        video.lpfnWndProc = DefWindowProcW;
        video.lpszClassName = L"AuroraVideo";
        video.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!RegisterClassW(&video)) throw std::runtime_error("Registro de janela de video falhou");
        RECT bounds{0, 0, 1152, 730};
        AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        window = CreateWindowExW(0, type.lpszClassName, L"Aurora | Biblioteca e nucleo PS1 experimental",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("Criacao de janela falhou");
        initControls();
        ShowWindow(window, show);
        UpdateWindow(window);
        MSG message{};
        int result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (font) DeleteObject(font);
        if (background) DeleteObject(background);
        return result < 0 ? 1 : int(message.wParam);
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, wide(error.what()).c_str(), L"Aurora", MB_OK | MB_ICONERROR);
        return 1;
    }
}
