using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using FO4RecordEditor.Models;

namespace FO4RecordEditor.Services;

public class ChatHistoryService
{
    private readonly string _dir;

    public ChatHistoryService()
    {
        _dir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Chats");
        if (!Directory.Exists(_dir)) Directory.CreateDirectory(_dir);
    }

    public List<ChatSession> LoadAll()
    {
        var list = new List<ChatSession>();
        foreach (var file in Directory.GetFiles(_dir, "*.json"))
        {
            try {
                var json = File.ReadAllText(file);
                var session = JsonSerializer.Deserialize<ChatSession>(json);
                if (session != null) list.Add(session);
            } catch {}
        }
        return list.OrderByDescending(x => x.CreatedAt).ToList();
    }

    public void Save(ChatSession session)
    {
        try {
            var file = Path.Combine(_dir, $"{session.Id}.json");
            File.WriteAllText(file, JsonSerializer.Serialize(session));
        } catch {}
    }

    public void Delete(Guid id)
    {
        try {
            var file = Path.Combine(_dir, $"{id}.json");
            if (File.Exists(file)) File.Delete(file);
        } catch {}
    }
}
