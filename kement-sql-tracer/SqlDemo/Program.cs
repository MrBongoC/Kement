using Microsoft.Data.Sqlite;

namespace SqlDemo;

// An app that talks to a database with no query logging of its own. Kement traces
// every SQL statement it runs, from outside, by hooking the command executor.
public static class Program
{
    public static void Main()
    {
        Console.WriteLine($"[sqldemo] pid={Environment.ProcessId}");
        using var con = new SqliteConnection("Data Source=:memory:");
        con.Open();

        Exec(con, "CREATE TABLE users(id INTEGER, name TEXT)");
        Exec(con, "INSERT INTO users(id,name) VALUES (1,'alice'),(2,'bob')");

        for (int i = 1; i <= 3; i++)
        {
            Query(con, $"SELECT name FROM users WHERE id = {i}");
            Thread.Sleep(200);
        }
        Console.WriteLine("[sqldemo] done");
        Thread.Sleep(300);
    }

    private static void Exec(SqliteConnection con, string sql)
    {
        using var cmd = con.CreateCommand();
        cmd.CommandText = sql;
        cmd.ExecuteNonQuery();
    }

    private static void Query(SqliteConnection con, string sql)
    {
        using var cmd = con.CreateCommand();
        cmd.CommandText = sql;
        using var r = cmd.ExecuteReader();
        while (r.Read()) { }
    }
}
