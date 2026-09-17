import React, { useState, useEffect, useRef } from 'react';

const API_BASE = '/api';

export default function App() {
  const [sql, setSql] = useState('SELECT * FROM users;');
  const [executing, setExecuting] = useState(false);
  const [result, setResult] = useState(null);
  const [error, setError] = useState(null);
  const [tables, setTables] = useState([]);
  const [schemaFilter, setSchemaFilter] = useState('');
  const [selectedTable, setSelectedTable] = useState(null);
  const [tableSchema, setTableSchema] = useState(null);
  const [schemaLoading, setSchemaLoading] = useState(false);
  const [serverHealth, setServerHealth] = useState(null);
  const [activeResultTab, setActiveResultTab] = useState('grid');
  const [history, setHistory] = useState(() => {
    try {
      const saved = localStorage.getItem('emberdb_query_history');
      return saved ? JSON.parse(saved) : [];
    } catch {
      return [];
    }
  });

  const textareaRef = useRef(null);
  const gutterRef = useRef(null);

  // Fetch health and tables on mount
  useEffect(() => {
    checkHealth();
    fetchTables();
  }, []);

  const checkHealth = async () => {
    try {
      const res = await fetch(`${API_BASE}/health`);
      if (res.ok) {
        const data = await res.json();
        setServerHealth(data);
      } else {
        setServerHealth({ status: 'error', engine: 'EmberDB' });
      }
    } catch (e) {
      setServerHealth({ status: 'offline', error: e.message });
    }
  };

  const fetchTables = async () => {
    try {
      const res = await fetch(`${API_BASE}/tables`);
      if (res.ok) {
        const data = await res.json();
        if (data.tables) {
          setTables(data.tables);
        }
      }
    } catch (err) {
      console.error('Failed to fetch tables:', err);
    }
  };

  const fetchSchema = async (tableName) => {
    if (selectedTable === tableName) {
      setSelectedTable(null);
      setTableSchema(null);
      return;
    }
    setSelectedTable(tableName);
    setSchemaLoading(true);
    try {
      const res = await fetch(`${API_BASE}/schema/${tableName}`);
      if (res.ok) {
        const data = await res.json();
        setTableSchema(data);
      } else {
        setTableSchema(null);
      }
    } catch (err) {
      console.error('Failed to fetch schema:', err);
      setTableSchema(null);
    } finally {
      setSchemaLoading(false);
    }
  };

  const executeQuery = async () => {
    const trimmed = sql.trim();
    if (!trimmed) return;

    setExecuting(true);
    setError(null);

    try {
      const res = await fetch(`${API_BASE}/query`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sql: trimmed }),
      });

      const data = await res.json();

      if (res.ok && data.success) {
        setResult(data);
        setError(null);
        if (data.columns && data.columns.length > 0) {
          setActiveResultTab('grid');
        } else {
          setActiveResultTab('messages');
        }
        // Refresh tables list if DDL/DML was executed
        fetchTables();
      } else {
        setError(data.error || 'Query execution failed');
        setResult(null);
        setActiveResultTab('messages');
      }

      // Record in history
      setHistory((prev) => {
        const updated = [
          {
            sql: trimmed,
            time: new Date().toLocaleTimeString(),
            success: Boolean(res.ok && data.success),
            executionTimeMs: data?.executionTimeMs,
          },
          ...prev.filter((h) => h.sql !== trimmed),
        ].slice(0, 30);
        try {
          localStorage.setItem('emberdb_query_history', JSON.stringify(updated));
        } catch {}
        return updated;
      });
    } catch (err) {
      setError(`Network error: ${err.message}. Ensure emberdb_server is running.`);
      setResult(null);
      setActiveResultTab('messages');
    } finally {
      setExecuting(false);
    }
  };

  const loadTemplate = (text) => {
    setSql(text);
    if (textareaRef.current) {
      textareaRef.current.focus();
    }
  };

  const formatSql = () => {
    // Client-side basic keyword uppercase formatting
    const keywords = [
      'SELECT', 'FROM', 'WHERE', 'INSERT', 'INTO', 'VALUES', 'UPDATE', 'SET',
      'DELETE', 'CREATE', 'TABLE', 'DROP', 'INDEX', 'ON', 'JOIN', 'INNER',
      'LEFT', 'RIGHT', 'GROUP', 'BY', 'ORDER', 'ASC', 'DESC', 'LIMIT',
      'AND', 'OR', 'NOT', 'NULL', 'IS', 'AS', 'COUNT', 'SUM', 'AVG', 'MIN',
      'MAX', 'INT', 'INTEGER', 'BIGINT', 'DOUBLE', 'BOOLEAN', 'VARCHAR'
    ];
    let formatted = sql;
    keywords.forEach((kw) => {
      const regex = new RegExp(`\\b${kw}\\b`, 'gi');
      formatted = formatted.replace(regex, kw);
    });
    setSql(formatted);
  };

  const exportCSV = () => {
    if (!result || !result.columns || !result.rows || result.rows.length === 0) return;
    const escapeVal = (val) => {
      if (val === null || val === undefined) return '';
      const s = String(val);
      if (s.includes(',') || s.includes('"') || s.includes('\n')) {
        return `"${s.replace(/"/g, '""')}"`;
      }
      return s;
    };
    const header = result.columns.map(escapeVal).join(',');
    const rows = result.rows.map((row) => row.map(escapeVal).join(',')).join('\n');
    const csv = `${header}\n${rows}`;
    const blob = new Blob([csv], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.setAttribute('href', url);
    link.setAttribute('download', `emberdb_export_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
  };

  const handleEditorScroll = (e) => {
    if (gutterRef.current) {
      gutterRef.current.scrollTop = e.target.scrollTop;
    }
  };

  const handleKeyDown = (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      executeQuery();
    } else if (e.key === 'Tab') {
      e.preventDefault();
      const start = e.target.selectionStart;
      const end = e.target.selectionEnd;
      const newSql = sql.substring(0, start) + '  ' + sql.substring(end);
      setSql(newSql);
      setTimeout(() => {
        if (textareaRef.current) {
          textareaRef.current.selectionStart = textareaRef.current.selectionEnd = start + 2;
        }
      }, 0);
    }
  };

  // Line numbers calculation
  const lineCount = Math.max(1, sql.split('\n').length);
  const lineNumbers = Array.from({ length: lineCount }, (_, i) => i + 1);

  // Filtered tables
  const filteredTables = tables.filter((t) =>
    t.toLowerCase().includes(schemaFilter.trim().toLowerCase())
  );

  const isOnline = serverHealth?.status === 'ok';

  return (
    <div className="app-container">
      {/* Top Header */}
      <header className="app-header">
        <div className="header-left">
          <div className="brandmark">
            <div className="brand-icon">▲</div>
            <span className="brand-title">
              EMBER<span className="brand-title-sub">DB</span>
            </span>
          </div>

          <span className="header-divider">/</span>

          <div className="context-pill">
            <span className={`status-dot ${isOnline ? '' : 'offline'}`}></span>
            <span>{serverHealth?.engine || 'EmberDB'}</span>
            <span className="header-divider">/</span>
            <span className="context-version">{serverHealth?.version ? `v${serverHealth.version}` : 'local'}</span>
          </div>

          {result?.executionTimeMs !== undefined && (
            <div className="header-telemetry">
              <span>LATENCY <strong className="telemetry-val">{result.executionTimeMs}MS</strong></span>
            </div>
          )}
        </div>

        <div className="header-right">
          <button
            className="btn-sync"
            onClick={() => {
              checkHealth();
              fetchTables();
            }}
            title="Sync schema and check health"
          >
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" strokeLinecap="round" strokeLinejoin="round" />
            </svg>
            <span>Sync</span>
          </button>
          <div className="header-avatar">01</div>
        </div>
      </header>

      {/* Main Layout */}
      <div className="main-layout">
        {/* Left Sidebar */}
        <aside className="sidebar">
          {/* Schema Search Bar */}
          <div className="sidebar-search-container">
            <div className="search-input-wrapper">
              <svg className="search-icon" width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                <path d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
              <input
                className="sidebar-search-input"
                type="text"
                placeholder="Filter schema..."
                value={schemaFilter}
                onChange={(e) => setSchemaFilter(e.target.value)}
              />
              <span className="search-shortcut-badge">/</span>
            </div>
          </div>

          {/* Navigation Trees */}
          <div className="sidebar-nav-scroll">
            {/* Tables Section */}
            <div>
              <div className="sidebar-section-header">
                <span className="section-label">TABLES ({filteredTables.length})</span>
                <button className="btn-icon-xs" onClick={fetchTables} title="Refresh Tables">
                  <svg width="10" height="10" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" strokeLinecap="round" strokeLinejoin="round" />
                  </svg>
                </button>
              </div>

              {filteredTables.length === 0 ? (
                <p className="empty-state-text">No tables found</p>
              ) : (
                <ul className="table-list">
                  {filteredTables.map((tbl) => {
                    const isSelected = selectedTable === tbl;
                    return (
                      <React.Fragment key={tbl}>
                        <li
                          className={`table-item ${isSelected ? 'active' : ''}`}
                          onClick={() => fetchSchema(tbl)}
                          title={`Click to inspect ${tbl}`}
                        >
                          <div className="table-name-group">
                            <span className="table-icon">▤</span>
                            <span>{tbl}</span>
                          </div>
                        </li>

                        {/* Schema Column Inspector Drawer */}
                        {isSelected && (
                          <div className="schema-drawer">
                            <div className="schema-drawer-header">
                              <span>COLUMNS</span>
                              <button
                                className="btn-icon-xs"
                                onClick={(e) => {
                                  e.stopPropagation();
                                  setSelectedTable(null);
                                }}
                              >
                                ×
                              </button>
                            </div>
                            {schemaLoading ? (
                              <p className="empty-state-text">Loading...</p>
                            ) : tableSchema?.columns ? (
                              <table className="schema-table">
                                <thead>
                                  <tr>
                                    <th>Column</th>
                                    <th>Type</th>
                                  </tr>
                                </thead>
                                <tbody>
                                  {tableSchema.columns.map((c) => (
                                    <tr key={c.name}>
                                      <td className="col-name">{c.name}</td>
                                      <td className="col-type">{c.type}</td>
                                    </tr>
                                  ))}
                                </tbody>
                              </table>
                            ) : (
                              <p className="empty-state-text">No columns</p>
                            )}
                          </div>
                        )}
                      </React.Fragment>
                    );
                  })}
                </ul>
              )}
            </div>

            {/* Quick Templates */}
            <div className="templates-section">
              <div className="sidebar-section-header" style={{ padding: '0 0 6px 0' }}>
                <span className="section-label">TEMPLATES</span>
              </div>
              <div className="templates-container">
                <button
                  className="template-btn"
                  onClick={() => loadTemplate('SELECT * FROM users;')}
                >
                  SELECT
                </button>
                <button
                  className="template-btn"
                  onClick={() =>
                    loadTemplate(
                      "CREATE TABLE users (\n    id INT,\n    name VARCHAR,\n    age INT\n);"
                    )
                  }
                >
                  CREATE TABLE
                </button>
                <button
                  className="template-btn"
                  onClick={() =>
                    loadTemplate(
                      "INSERT INTO users VALUES (1, 'Faizaan', 23);\nINSERT INTO users VALUES (2, 'Ahmed', 25);"
                    )
                  }
                >
                  INSERT
                </button>
                <button
                  className="template-btn"
                  onClick={() => loadTemplate('SELECT * FROM users WHERE age >= 20;')}
                >
                  WHERE
                </button>
                <button
                  className="template-btn"
                  onClick={() =>
                    loadTemplate(
                      'SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age DESC;'
                    )
                  }
                >
                  GROUP BY
                </button>
                <button
                  className="template-btn"
                  onClick={() =>
                    loadTemplate(
                      'SELECT users.name, orders.amount FROM users INNER JOIN orders ON users.id = orders.user_id;'
                    )
                  }
                >
                  JOIN
                </button>
              </div>
            </div>

            {/* Query History */}
            <div className="history-section">
              <div className="sidebar-section-header" style={{ padding: '0 0 6px 0' }}>
                <span className="section-label">HISTORY ({history.length})</span>
                {history.length > 0 && (
                  <button
                    className="btn-clear-history"
                    onClick={() => {
                      setHistory([]);
                      localStorage.removeItem('emberdb_query_history');
                    }}
                    title="Clear history"
                  >
                    Clear
                  </button>
                )}
              </div>

              {history.length === 0 ? (
                <p className="empty-state-text">No executed queries</p>
              ) : (
                <ul className="history-list">
                  {history.map((h, idx) => (
                    <li
                      key={idx}
                      className="history-card"
                      onClick={() => setSql(h.sql)}
                      title={h.sql}
                    >
                      <div className="history-header-row">
                        <span>{h.time}</span>
                        <span className={`history-status-chip ${h.success ? '' : 'err'}`}>
                          {h.executionTimeMs !== undefined ? `${h.executionTimeMs}MS` : h.success ? 'OK' : 'ERR'}
                        </span>
                      </div>
                      <p className="history-query-text">{h.sql}</p>
                    </li>
                  ))}
                </ul>
              )}
            </div>
          </div>

          {/* Sidebar Footer */}
          <div className="sidebar-footer">
            <div className="footer-online-indicator">
              <span className={`footer-online-dot ${isOnline ? '' : 'offline'}`}></span>
              <span>{isOnline ? 'ONLINE' : 'OFFLINE'}</span>
            </div>
            <span>EmberDB</span>
          </div>
        </aside>

        {/* Main Workspace */}
        <main className="main-workspace">
          {/* Editor Tabs Header */}
          <div className="editor-tabs-bar">
            <div className="tabs-left">
              <div className="editor-tab">
                <span>active_query.sql</span>
                <span
                  style={{ cursor: 'pointer', color: '#737373' }}
                  onClick={() => setSql('')}
                  title="Clear editor"
                >
                  ×
                </span>
              </div>
            </div>

            <div className="tabs-right">
              <button className="btn-tab-action" onClick={formatSql} title="Format SQL keywords">
                FORMAT
              </button>
              <button className="btn-tab-action" onClick={() => setSql('')} title="Clear query">
                CLEAR
              </button>
            </div>
          </div>

          {/* SQL Editor Pane */}
          <section className="editor-pane">
            <div className="editor-canvas">
              {/* Line Numbers Gutter */}
              <div className="editor-gutter" ref={gutterRef}>
                {lineNumbers.map((num) => (
                  <div key={num}>{num}</div>
                ))}
              </div>

              {/* Textarea Code Input */}
              <textarea
                ref={textareaRef}
                className="editor-textarea"
                value={sql}
                onChange={(e) => setSql(e.target.value)}
                onScroll={handleEditorScroll}
                onKeyDown={handleKeyDown}
                placeholder="-- Enter SQL statement here..."
                spellCheck={false}
              />
            </div>

            {/* Action Bar */}
            <div className="editor-action-bar">
              <div className="action-bar-left">
                <button
                  className="btn-execute"
                  onClick={executeQuery}
                  disabled={executing || !sql.trim()}
                >
                  <svg width="10" height="10" viewBox="0 0 24 24" fill="currentColor">
                    <path d="M8 5v14l11-7z" />
                  </svg>
                  <span>{executing ? 'Executing...' : 'Execute'}</span>
                  <span className="execute-shortcut-chip">⌘↵</span>
                </button>

                <button
                  className="btn-secondary-action"
                  onClick={() => setSql('')}
                  disabled={executing || !sql}
                >
                  Clear
                </button>
              </div>

              <div className="action-bar-right">
                {result && (
                  <>
                    {result.executionTimeMs !== undefined && (
                      <span>
                        <strong className="telemetry-highlight">{result.executionTimeMs}ms</strong>
                      </span>
                    )}
                    {result.rows !== undefined && (
                      <>
                        <span>•</span>
                        <span>
                          {result.rows.length} {result.rows.length === 1 ? 'row' : 'rows'} returned
                        </span>
                      </>
                    )}
                    {result.rowsAffected !== undefined && result.rows === undefined && (
                      <>
                        <span>•</span>
                        <span>
                          {result.rowsAffected} {result.rowsAffected === 1 ? 'row' : 'rows'} affected
                        </span>
                      </>
                    )}
                  </>
                )}
              </div>
            </div>
          </section>

          {/* Results Pane */}
          <section className="results-pane">
            {/* Sub-nav tabs */}
            <div className="results-subnav">
              <div className="results-tabs">
                <button
                  className={`results-tab-btn ${activeResultTab === 'grid' ? 'active' : ''}`}
                  onClick={() => setActiveResultTab('grid')}
                >
                  <span>DATA GRID</span>
                  <span className="count-badge">{result?.rows?.length ?? 0}</span>
                </button>
                <button
                  className={`results-tab-btn ${activeResultTab === 'messages' ? 'active' : ''}`}
                  onClick={() => setActiveResultTab('messages')}
                >
                  <span>MESSAGES</span>
                </button>
              </div>

              <div className="results-actions">
                <button
                  className="btn-export"
                  onClick={exportCSV}
                  disabled={!result?.columns || !result?.rows || result.rows.length === 0}
                  title="Export results to CSV"
                >
                  <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-4l-4 4m0 0l-4-4m4 4V4" strokeLinecap="round" strokeLinejoin="round" />
                  </svg>
                  <span>Export CSV</span>
                </button>
              </div>
            </div>

            {/* Error Banner */}
            {error && (
              <div className="error-banner">
                <div className="error-title">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" strokeLinecap="round" strokeLinejoin="round" />
                  </svg>
                  <span>Query Error</span>
                </div>
                <div className="error-message-text">{error}</div>
              </div>
            )}

            {/* Data Grid Tab Content */}
            {activeResultTab === 'grid' && (
              <div className="data-grid-wrapper">
                {result?.columns && result.columns.length > 0 ? (
                  <table className="data-grid-table">
                    <thead>
                      <tr>
                        <th className="col-index-header">#</th>
                        {result.columns.map((col, idx) => (
                          <th key={idx}>
                            <div className="col-header-content">
                              <span>{col}</span>
                            </div>
                          </th>
                        ))}
                      </tr>
                    </thead>
                    <tbody>
                      {result.rows && result.rows.length > 0 ? (
                        result.rows.map((row, rIdx) => (
                          <tr key={rIdx}>
                            <td className="col-index-cell">{rIdx + 1}</td>
                            {row.map((cell, cIdx) => (
                              <td key={cIdx}>
                                {cell === null ? (
                                  <span className="null-indicator">NULL</span>
                                ) : (
                                  String(cell)
                                )}
                              </td>
                            ))}
                          </tr>
                        ))
                      ) : (
                        <tr>
                          <td
                            colSpan={result.columns.length + 1}
                            style={{ textAlign: 'center', padding: '24px', color: '#737373' }}
                          >
                            Empty set (0 rows)
                          </td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                ) : result?.rowsAffected !== undefined ? (
                  <div className="statement-message-view">
                    <div className="statement-success-title">Query executed successfully.</div>
                    <div>{result.rowsAffected} {result.rowsAffected === 1 ? 'row' : 'rows'} affected.</div>
                    {result.executionTimeMs !== undefined && (
                      <div>Duration: {result.executionTimeMs}ms</div>
                    )}
                  </div>
                ) : (
                  <div className="statement-message-view">
                    <div>No query executed yet. Enter SQL above and press Execute (⌘↵).</div>
                  </div>
                )}
              </div>
            )}

            {/* Messages Tab Content */}
            {activeResultTab === 'messages' && (
              <div className="data-grid-wrapper">
                <div className="statement-message-view">
                  {error ? (
                    <div>
                      <div style={{ color: '#f87171', fontWeight: 600, marginBottom: '6px' }}>
                        Execution Failed
                      </div>
                      <pre style={{ color: '#fca5a5', fontFamily: 'inherit' }}>{error}</pre>
                    </div>
                  ) : result ? (
                    <div>
                      <div className="statement-success-title">Query Completed</div>
                      {result.rowsAffected !== undefined && (
                        <div>Rows affected: {result.rowsAffected}</div>
                      )}
                      {result.rows !== undefined && (
                        <div>Rows returned: {result.rows.length}</div>
                      )}
                      {result.executionTimeMs !== undefined && (
                        <div>Execution time: {result.executionTimeMs}ms</div>
                      )}
                      <div style={{ marginTop: '12px', color: '#525252' }}>
                        SQL: <code>{sql}</code>
                      </div>
                    </div>
                  ) : (
                    <div>System ready. Execute a statement to view messages.</div>
                  )}
                </div>
              </div>
            )}

            {/* Grid Status Footer */}
            <footer className="results-footer">
              <div className="footer-left">
                {result?.rows ? (
                  <span>
                    Showing <strong className="footer-stat-bold">1–{result.rows.length}</strong> of{' '}
                    <strong className="footer-stat-bold">{result.rows.length}</strong> rows
                  </span>
                ) : result?.rowsAffected !== undefined ? (
                  <span>
                    <strong className="footer-stat-bold">{result.rowsAffected}</strong> rows affected
                  </span>
                ) : (
                  <span>Ready</span>
                )}
              </div>

              <div className="footer-right">
                {result?.executionTimeMs !== undefined && (
                  <span>{result.executionTimeMs}ms</span>
                )}
              </div>
            </footer>
          </section>
        </main>
      </div>
    </div>
  );
}
