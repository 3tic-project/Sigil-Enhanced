#pragma once
#ifndef FINDREPLACEPLUS_H
#define FINDREPLACEPLUS_H

#include <QTimer>

#include "ui_FindReplacePlus.h"
#include "BookManipulation/FolderKeeper.h"
#include "MainUI/MainWindow.h"
#include "Misc/SearchOperations.h"
#include "MiscEditors/SearchEditorModelPlus.h"
#include "ViewEditors/Searchable.h"

class QMenu;
class QAction;
class Resource;
class MainWindow;

class FindReplacePlus : public QWidget
{
    Q_OBJECT

public:
    FindReplacePlus(MainWindow *main_window);
    ~FindReplacePlus();

    /**
     * Defines possible search target areas.
     */
    enum LookWhere {
        LookWhere_CurrentFile = 0,
        LookWhere_AllHTMLFiles,
        LookWhere_AllCSSFiles,
        LookWhere_SelectedFiles,
        LookWhere_OPFFile,
        LookWhere_NCXFile
    };

    enum SearchMode {
        // Normal is Case insensitive
        SearchMode_Normal = 0,
        SearchMode_Regex,
        SearchMode_PreSearch
    };

    enum Direction {
        Direction_Up = 0,
        Direction_Down
    };

    /**
     * Sets up the default Find text during dialog creation.
     */
    void SetUpFindText();

    void ShowHide();

    /**
     * Utility Routines to simplify logical testing
     */
    inline bool isWhereHTML();
    inline bool isWhereCSS();
    inline bool isWhereSelected();
    inline bool isWhereOPF();
    inline bool isWhereNCX();
    inline bool isWhereCF();

    QString GetPreSearchRegex();
    QString GetSearchRegex();
    QString GetReplace();
    QList<Resource*> GetAllResourcesToSearch();
    void EmitOpenFileRequest(const QString& bookpath, int line, int pos);

public slots:
    void close();
    void show();

    void LoadSearchByName(const QString &name);
    void LoadSearch(SearchEditorModelPlus::searchEntry *search_entry);
    void FindSearch();
    void ReplaceCurrentSearch();
    void ReplaceSearch();
    void CountAllSearch();
    void ReplaceAllSearch();
    void DoRestart();

    // Shows a message in the main window.
    void ShowMessage(const QString &message);
    void clearMessage();

    void DryRunComplete() { m_DryRunRunning = false; clearMessage(); };

    void SetOptionWrap(bool new_state);
    void SetOptionPreview(bool new_state);

    bool FindAnyText(QString text, bool escape = true);
    void FindAnyTextInTags(QString text);

    void ShowHideMarkedText(bool marked);

    void HideFindReplace();

    void ValidateRegex();
    void ValidatePreRegex();

    void CountsReportCount(SearchEditorModelPlus::searchEntry* entry, int& count);

    void TokeniseSelection();

signals:

    void OpenSearchEditorRequest(SearchEditorModelPlus::searchEntry *search_entry = NULL);

    void ShowMessageRequest(const QString &message);

    void FROpenFileRequest(const QString &bookpath, int line, int offset);

    /**
     * Emitted when we want to do some operations with the clipboard
     * to paste things, but restoring state afterwards so that the
     * Clipboard History and current clipboard is left unaffected.
     */
    void ClipboardSaveRequest();
    void ClipboardRestoreRequest();

    void AskWhyGetEmptyEntries();

protected:
    void keyPressEvent(QKeyEvent *event);

private slots:

    bool IsMarkedText();

    void FindClicked();
    void CountClicked();
    void ReplaceClicked();
    void ReplaceAllClicked();
    void RestartClicked();

    // Uses the find direction to determine if we should find next
    // or previous.
    bool Find();
    bool DoFindNext();
    bool DoFindPrevious();

    // Counts the number of occurrences of the user's
    // term in the document.
    int Count();

    // Uses the find direction to determine if we should replace next
    // or previous.
    bool Replace();

    // Replaces the user's search term with the user's
    // replacement text if a match is selected. If it's not,
    // calls FindNext() so it becomes selected.
    bool ReplaceCurrent();
    bool DoReplaceNext();
    bool DoReplacePrevious();

    // Does a Dry Run Find A// / Replace All  and shows results in table
    void PerformDryRunReplace();

    // Allows a user to choose which matches in Replace All should
    // be applied
    void ChooseReplacements();

    // Replaces the user's search term with the user's
    // replacement text in the entire document. Shows a
    // dialog telling how many occurrences were replaced.
    int ReplaceAll();

    void FindNextInFile();
    void ReplaceNextInFile();
    void ReplaceAllInFile();
    void CountInFile();

    void expireMessage();

    void SaveSearchAction();

    void ClearHistory();

    void UpdatePreSearchUI();

private:
    void SetPreviousSearch();
    bool IsNewSearch();

    bool SetStartingResource(bool open_starting_tab = false);
    void OpenResourceTabForSearch(Resource* resource);

    QString GetControls();
    bool FindText(Direction direction);
    bool ReplaceText(Direction direction, bool replace_current = false);

    void SetCodeViewIfNeeded();

    // void RestoreFRFocusIfNeeded(bool had_focus, bool force=false);

    // Displays a message to the user informing him
    // that his last search term could not be found.
    void CannotFindSearchTerm();

    Searchable *GetAvailableSearchable();

    QList <Resource *> GetFilesToSearch(bool force_all = false);

    QString TokeniseForRegex(const QString& text, bool includeNumerics);

    bool IsCurrentFileInSelection();

    void SetKeyModifiers();

    void ResetKeyModifiers();

    int CountInFiles();

    bool GetSearchableAndFindNext(bool marked_text, int split_at, bool inRemainder);

    int ReplaceInAllFiles();

    bool FindInAllFiles(Direction direction);

    Resource *GetNextContainingResource(Direction direction);

    Resource *GetNextResource(Resource *current_resource, Direction direction);

    Resource *GetCurrentResource();

    int GetStartingPos();

    void SetSearchMode(int search_mode);
    void SetLookWhere(int look_where);
    void SetSearchDirection(int search_direction);

    bool ResourceContainsCurrentRegex(Resource* resource);

    void SearchEndingProcess();
    /**
     * Returns a list of all the strings
     * currently stored in the PreFind combo box.
     *
     * @return The stored find strings.
     */
    QStringList GetPreviousPreFindStrings();

    /**
     * Returns a list of all the strings
     * currently stored in the find combo box.
     *
     * @return The stored find strings.
     */
    QStringList GetPreviousFindStrings();

    /**
     * Returns a list of all the strings
     * currently stored in the replace combo box.
     *
     * @return The stored replace strings.
     */
    QStringList GetPreviousReplaceStrings();

    /**
     * Updates the PreFind combo box with the
     * currently typed-in string.
     */
    void UpdatePreviousPreFindStrings(const QString& text = QString());

    /**
     * Updates the find combo box with the
     * currently typed-in string.
     */
    void UpdatePreviousFindStrings(const QString &text = QString());

    /**
     * Updates the replace combo box with the
     * currently typed-in string.
     */
    void UpdatePreviousReplaceStrings(const QString &text = QString());

    void UpdateSearchControls(const QString &text = QString());

    FindReplacePlus::LookWhere GetLookWhere();
    FindReplacePlus::SearchMode GetSearchMode();
    FindReplacePlus::Direction GetSearchDirection();

    // Checks if Find is empty when not checking spelling
    bool IsValidFindText();

    // Reads all the stored dialog settings
    void ReadSettings();

    // Writes all the stored dialog settings
    void WriteSettings();

    // void ShowHideAdvancedOptions();

    // Set all F&R buttons to text-only
    // Default: icon-only
    void SetFRButtonsTextOnly();

    void ExtendUI();

    void WriteSettingsVisible(bool visible);

    /**
     * Connects all the required signals to their respective slots.
     */
    void ConnectSignalsToSlots();

    void SetFocusPreFind();
    void SetFocusFind();
    void SetFocusReplace();
    bool HasFocusPreFind();
    bool HasFocusFind();
    bool HasFocusReplace();

    void warningEmptyExpression();
    void warningEndedSearch();
    ///////////////////////////////
    // PRIVATE MEMBER VARIABLES
    ///////////////////////////////

    // A const reference to the mainwindow that
    // spawned this widget. Needed for searching.
    MainWindow *m_MainWindow;

    QTimer m_timer;

    Ui::FindReplacePlus ui;

    bool m_OptionWrap;

    bool m_OptionPreview;

    bool m_LookWhereCurrentFile;

    QString m_LastFindText;

    bool m_IsSearchGroupRunning;

    // Save the lasted search regex and options for
    // determining if the search options has changed.
    QStringList m_PreviousSearch;

    Resource * m_StartingResource;

    int m_StartingPos;

    bool m_InRemainder;

    bool m_SearchRunning;

    bool m_DryRunRunning;

    bool m_ShiftUsed;

    int m_StartingTextLen;
};
#endif // FINDREPLACEPLUS_H
