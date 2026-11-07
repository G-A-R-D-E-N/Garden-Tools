import { useEffect, useState } from 'react';
import { ClipboardCopy, MessageSquare, Terminal, Check } from 'lucide-react';
import './ChatPanel.css';
import './NativeMcpPanel.css';

const MCP_COMMAND = 'fo4ide --mcp';

export default function ChatPanel() {
  const [prompt, setPrompt] = useState('');
  const [copied, setCopied] = useState<'command' | 'prompt' | ''>('');

  useEffect(() => {
    const handler = (event: Event) => {
      const detail = (event as CustomEvent).detail;
      if (typeof detail === 'string' && detail.trim()) {
        setPrompt(detail.trim());
        setCopied('');
      }
    };
    window.addEventListener('fo4:ask-ai', handler);
    return () => window.removeEventListener('fo4:ask-ai', handler);
  }, []);

  const copy = async (kind: 'command' | 'prompt', value: string) => {
    try {
      await navigator.clipboard.writeText(value);
      setCopied(kind);
      window.setTimeout(() => setCopied(current => current === kind ? '' : current), 1200);
    } catch {
      setCopied('');
    }
  };

  return (
    <div className="ai-panel">
      <div className="chat-toolbar">
        <MessageSquare size={15} />
        <span className="chat-native-title">AI via native MCP</span>
      </div>

      <div className="ai-content chat-native-content">
        <div className="ai-chat-bubble system">
          Embedded provider chat is not part of the native C++ host. FO4IDE exposes its plugin,
          Papyrus, archive, material, audio, NIF, and other tools through the native stdio MCP server.
        </div>

        <div className="chat-native-card">
          <div className="chat-native-card-title"><Terminal size={13} /> Start the MCP server</div>
          <code className="chat-native-command">{MCP_COMMAND}</code>
          <button className="chat-native-action" onClick={() => copy('command', MCP_COMMAND)}>
            {copied === 'command' ? <Check size={13} /> : <ClipboardCopy size={13} />}
            {copied === 'command' ? 'Copied' : 'Copy command'}
          </button>
          <div className="chat-native-note">
            Configure Claude, Codex, or another MCP client to launch that command over stdio.
            AI inference and conversation storage stay in the external client; FO4IDE supplies native tools only.
          </div>
        </div>

        {prompt ? (
          <div className="chat-native-card">
            <div className="chat-native-card-title">Prompt from FO4IDE</div>
            <pre className="chat-native-prompt">{prompt}</pre>
            <button className="chat-native-action" onClick={() => copy('prompt', prompt)}>
              {copied === 'prompt' ? <Check size={13} /> : <ClipboardCopy size={13} />}
              {copied === 'prompt' ? 'Copied' : 'Copy prompt'}
            </button>
          </div>
        ) : (
          <div className="chat-native-note chat-native-empty">
            Record actions such as “Ask AI: what breaks?” will place their generated prompt here for an MCP-connected client.
          </div>
        )}
      </div>
    </div>
  );
}
