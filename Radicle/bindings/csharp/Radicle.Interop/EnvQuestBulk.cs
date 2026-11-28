using System;
using System.Collections.Generic;
using Radicle.Interop.Native;

namespace Radicle.Interop;

public sealed record EnvQuestBulkRow(FormKey Key, bool StartGameEnabled, QuestStoredStructure Structure);

public static unsafe class EnvQuestBulk
{
    public static IReadOnlyList<EnvQuestBulkRow> Read(RadicleEnvironment env)
    {
        ArgumentNullException.ThrowIfNull(env);
        using var use = env.Use();
        QuestBulkRow* rows;
        nuint count;
        Call.Check(RadicleNative.EnvQuestBulk(use.Pointer, &rows, &count));
        try
        {
            var winners = new EnvQuestBulkRow[checked((int)count)];
            for (var i = 0; i < winners.Length; ++i)
            {
                QuestStructureView view = default;
                Call.Check(RadicleNative.QuestStructureGetView(rows[i].Structure, &view));
                winners[i] = new EnvQuestBulkRow(FormKey.Required(rows[i].Key, "quest"), rows[i].StartGameEnabled != 0,
                    QuestStructure.FromView(view));
            }

            return winners;
        }
        finally
        {
            RadicleNative.EnvQuestBulkFree(rows, count);
        }
    }
}