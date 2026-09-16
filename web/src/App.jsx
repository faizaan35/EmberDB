import React, { useState, useEffect } from 'react';

const API_BASE = '/api';

export default function App() {
  const [sql, setSql] = useState('SELECT * FROM users;');
  const [executing, setExecuting] = useState(false);
  const [result, setResult] = useState(null);
  const [error, setError] = useState(null);
  const [tables, setTables] = useState([]);
  const [selectedTable, setSelectedTable] = useState(null);
  const [tableSchema, setTableSchema] = useState(null);
  const [schemaLoading, setSchemaLoading] = useState(false);
  const [serverHealth, setServerHealth] = useState(null);
  const [history, setHistory] = useState(() => {
    try {
      const saved = localStorage.getItem('emberdb_query_history');
      return saved ? JSON.parse(saved) : [];
    } catch {
      return [];
    }
  });

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
        setServerHealth({ status: 'error', engine: 'Unknown' });
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
    setResult(null);

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
        // Refresh tables list if DDL/DML was executed
        fetchTables();
      } else {
        setError(data.error || 'Query execution failed');
        setResult(null);
      }

      // Record in history
      setHistory((prev) => {
        const updated = [
          {
            sql: trimmed,
            time: new Date().toLocaleTimeString(),
            success: data.success,
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
    } finally {
      setExecuting(false);
    }
  };

  const loadTemplate = (text) => {
    setSql(text);
  };

  return (
    <div className="app-container">
      {/* Header */}
      <header className="app-header">
        <div className="header-brand">
          <span className="brand-logo">🔥</span>
          <h1>EmberDB</h1>
          <span className="badge">Web Console</span>
        </div>
        <div className="header-status">
          {serverHealth ? (
            serverHealth.status === 'ok' ? (
              <span className="status-indicator online">
                ● Connected ({serverHealth.engine} v{serverHealth.version})
              </span>
            ) : (
              <span className="status-indicator offline">● Server Offline</span>
            )
          ) : (
            <span className="status-indicator connecting">○ Connecting...</span>
          )}
          <button className="btn-small" onClick={() => { checkHealth(); fetchTables(); }}>
            ↻ Refresh
          </button>
        </div>
      </header>

      {/* Main Layout */}
      <div className="main-layout">
        {/* Sidebar */}
        <aside className="sidebar">
          {/* Tables Section */}
          <div className="sidebar-section">
            <div className="section-title">
              <span>Catalog Tables ({tables.length})</span>
              <button className="btn-xs" onClick={fetchTables} title="Refresh tables">↻</button>
            </div>
            {tables.length === 0 ? (
              <p className="empty-text">No tables found</p>
            ) : (
              <ul className="table-list">
                {tables.map((tbl) => (
                  <li
                    key={tbl}
                    className={`table-item ${selectedTable === tbl ? 'active' : ''}`}
                    onClick={() => fetchSchema(tbl)}
                  >
                    <span>📊 {tbl}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>

          {/* Schema Inspector */}
          {selectedTable && (
            <div className="sidebar-section schema-section">
              <div className="section-title">
                <span>Schema: {selectedTable}</span>
                <button className="btn-xs" onClick={() => setSelectedTable(null)}>×</button>
              </div>
              {schemaLoading ? (
                <p className="empty-text">Loading schema...</p>
              ) : tableSchema && tableSchema.columns ? (
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
                <p className="empty-text">Failed to load schema</p>
              )}
            </div>
          )}

          {/* Quick Query Templates */}
          <div className="sidebar-section">
            <div className="section-title">
              <span>Sample Queries</span>
            </div>
            <div className="template-chips">
              <button
                className="chip"
                onClick={() =>
                  loadTemplate(
                    "CREATE TABLE users (\n    id INT,\n    name VARCHAR,\n    age INT\n);"
                  )
                }
              >
                CREATE TABLE
              </button>
              <button
                className="chip"
                onClick={() =>
                  loadTemplate(
                    "INSERT INTO users VALUES (1, 'Faizaan', 23);\nINSERT INTO users VALUES (2, 'Ahmed', 25);"
                  )
                }
              >
                INSERT
              </button>
              <button
                className="chip"
                onClick={() => loadTemplate("SELECT * FROM users WHERE age >= 20;")}
              >
                SELECT WHERE
              </button>
              <button
                className="chip"
                onClick={() =>
                  loadTemplate(
                    "SELECT age, COUNT(*) FROM users GROUP BY age ORDER BY age DESC;"
                  )
                }
              >
                GROUP BY + COUNT
              </button>
            </div>
          </div>

          {/* Query History */}
          <div className="sidebar-section history-section">
            <div className="section-title">
              <span>History ({history.length})</span>
              {history.length > 0 && (
                <button
                  className="btn-xs"
                  onClick={() => {
                    setHistory([]);
                    localStorage.removeItem('emberdb_query_history');
                  }}
                >
                  Clear
                </button>
              )}
            </div>
            {history.length === 0 ? (
              <p className="empty-text">No queries executed yet</p>
            ) : (
              <ul className="history-list">
                {history.map((h, idx) => (
                  <li
                    key={idx}
                    className={`history-item ${h.success ? 'success' : 'fail'}`}
                    onClick={() => setSql(h.sql)}
                    title={h.sql}
                  >
                    <span className="history-time">{h.time}</span>
                    <span className="history-sql">{h.sql}</span>
                  </li>
                ))}
              </ul>
            )}
          </div>
        </aside>

        {/* Content / Editor & Results */}
        <main className="content">
          {/* SQL Editor Area */}
          <div className="editor-card">
            <div className="editor-header">
              <label htmlFor="sql-editor" className="editor-label">
                SQL Query
              </label>
              <div className="editor-actions">
                <button
                  className="btn-clear"
                  onClick={() => setSql('')}
                  disabled={executing || !sql}
                >
                  Clear
                </button>
                <button
                  className="btn-primary"
                  onClick={executeQuery}
                  disabled={executing || !sql.trim()}
                >
                  {executing ? 'Executing...' : '▶ Execute SQL'}
                </button>
              </div>
            </div>
            <textarea
              id="sql-editor"
              className="sql-textarea"
              value={sql}
              onChange={(e) => setSql(e.target.value)}
              placeholder="Enter SQL statement (e.g. SELECT * FROM users;)"
              rows={6}
              onKeyDown={(e) => {
                if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
                  e.preventDefault();
                  executeQuery();
                }
              }}
            />
            <div className="editor-hint">
              <span>Tip: Press <code>Ctrl + Enter</code> to execute.</span>
            </div>
          </div>

          {/* Error Banner */}
          {error && (
            <div className="error-banner">
              <div className="error-icon">⚠️</div>
              <div className="error-body">
                <strong>Query Error</strong>
                <pre>{error}</pre>
              </div>
            </div>
          )}

          {/* Results Area */}
          {result && (
            <div className="result-card">
              <div className="result-header">
                <div className="result-stats">
                  <span className="stat-pill success">Query OK</span>
                  {result.executionTimeMs !== undefined && (
                    <span className="stat-pill">⏱ {result.executionTimeMs} ms</span>
                  )}
                  {result.rows !== undefined && (
                    <span className="stat-pill">
                      📋 {result.rows.length} {result.rows.length === 1 ? 'row' : 'rows'}
                    </span>
                  )}
                  {result.rowsAffected !== undefined && (
                    <span className="stat-pill">
                      ✏️ {result.rowsAffected} {result.rowsAffected === 1 ? 'row' : 'rows'} affected
                    </span>
                  )}
                </div>
              </div>

              {result.columns && result.columns.length > 0 ? (
                <div className="table-responsive">
                  <table className="results-table">
                    <thead>
                      <tr>
                        {result.columns.map((col, idx) => (
                          <th key={idx}>{col}</th>
                        ))}
                      </tr>
                    </thead>
                    <tbody>
                      {result.rows && result.rows.length > 0 ? (
                        result.rows.map((row, rIdx) => (
                          <tr key={rIdx}>
                            {row.map((cell, cIdx) => (
                              <td key={cIdx}>
                                {cell === null ? (
                                  <span className="null-val">NULL</span>
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
                            colSpan={result.columns.length}
                            className="empty-cell"
                          >
                            Empty set (0 rows)
                          </td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                </div>
              ) : (
                <div className="statement-ok">
                  <p>Statement executed successfully.</p>
                </div>
              )}
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
