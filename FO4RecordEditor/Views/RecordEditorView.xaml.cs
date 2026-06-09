using System.Windows.Controls;
using FO4RecordEditor.ViewModels;

namespace FO4RecordEditor.Views;

public partial class RecordEditorView : UserControl
{
    public RecordEditorView(RecordTabViewModel tab)
    {
        InitializeComponent();
        FieldList.ItemsSource = tab.Fields;
    }
}
