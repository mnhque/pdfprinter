using System;
using System.IO;

using System.Reflection;
using System.Windows;
using Microsoft.Win32;

namespace PDFPrinter.ConfigApp;

public partial class MainWindow : Window
{
    private readonly Settings _settings;

    public MainWindow()
    {
        InitializeComponent();
        _settings = Settings.Load();
        LoadIntoUi();

        var version = Assembly.GetExecutingAssembly().GetName().Version;
        VersionText.Text = $" {version?.Major}.{version?.Minor}.{version?.Build}";
    }

    private void LoadIntoUi()
    {
        OutputFolderBox.Text = _settings.DefaultOutputFolder;
        SelectComboItem(PaperSizeCombo, _settings.DefaultPaperSize);
        SelectComboItem(OrientationCombo, _settings.DefaultOrientation.ToString());
        AlwaysShowSaveDialogCheck.IsChecked = _settings.AlwaysShowSaveDialog;
        AutoNumberCheck.IsChecked = _settings.AutoNumberFilenames;
    }

    private static void SelectComboItem(System.Windows.Controls.ComboBox combo, string content)
    {
        foreach (System.Windows.Controls.ComboBoxItem item in combo.Items)
        {
            if (string.Equals(item.Content?.ToString(), content, StringComparison.OrdinalIgnoreCase))
            {
                combo.SelectedItem = item;
                return;
            }
        }
        combo.SelectedIndex = 0;
    }

    private void BrowseFolder_Click(object sender, RoutedEventArgs e)
    {
        // OpenFolderDialog is the modern (.NET 8 / WPF) native folder picker,
        // backed by IFileOpenDialog with FOS_PICKFOLDERS — no legacy
        // FolderBrowserDialog COM shim needed.
        var dialog = new OpenFolderDialog
        {
            Title = "Choose default PDF output folder",
            InitialDirectory = OutputFolderBox.Text,
        };
        if (dialog.ShowDialog() == true)
        {
            OutputFolderBox.Text = dialog.FolderName;
        }
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        _settings.DefaultOutputFolder = OutputFolderBox.Text;
        _settings.DefaultPaperSize = (PaperSizeCombo.SelectedItem as System.Windows.Controls.ComboBoxItem)?.Content?.ToString() ?? "Letter";
        _settings.DefaultOrientation = Enum.Parse<DefaultOrientation>(
            (OrientationCombo.SelectedItem as System.Windows.Controls.ComboBoxItem)?.Content?.ToString() ?? "Portrait");
        _settings.AlwaysShowSaveDialog = AlwaysShowSaveDialogCheck.IsChecked ?? true;
        _settings.AutoNumberFilenames = AutoNumberCheck.IsChecked ?? true;

        try
        {
            Directory.CreateDirectory(_settings.DefaultOutputFolder);
            _settings.Save();
            Close();
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, $"Could not save settings:\n{ex.Message}", "PDF Printer",
                MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void Cancel_Click(object sender, RoutedEventArgs e) => Close();

    private void About_Click(object sender, RoutedEventArgs e)
    {
        var version = Assembly.GetExecutingAssembly().GetName().Version;
        MessageBox.Show(this,
            $"PDF Printer {version?.Major}.{version?.Minor}.{version?.Build}\n\n" +
            "A virtual PDF printer for Windows.\n" +
            "Uses Ghostscript for XPS\u2192PDF conversion.\n\n" +
            "\u00A9 PDF Printer Project",
            "About PDF Printer", MessageBoxButton.OK, MessageBoxImage.Information);
    }
}
