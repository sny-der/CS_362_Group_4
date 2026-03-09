# The main for running our P2Ping Software. 

import threading
import time
import model.NetworkMain_windows as bridge
from view.gui import messaging_app

def setup_gui_logic(app):
    """Links GUI buttons to NetworkMain_windows functions."""
    
    def handle_connect():
        mode = app.get_connection_mode()
        if not mode: return
        
        if mode.lower() in ['local', 'l']:
            endpoint = bridge.prepare_local_endpoint()
            app.update_ip_display(endpoint)
            other = app.get_peer_address("Enter OTHER terminal's port:", endpoint)
            if other:
                bridge.establish_local_connection(other)
        else:
            endpoint = bridge.prepare_public_endpoint()
            app.update_ip_display(endpoint)
            other = app.get_peer_address("Enter OTHER device's [ipv6]:port:", endpoint)
            if other:
                bridge.establish_connection(other)
        
        app.after(0, app.show_chat_ui)

    def handle_send():
        msg = app.message_entry.get()
        if msg:
            bridge.send_message(msg)
            app.display_message("You", msg)
            app.message_entry.delete(0, 'end')

    # Assign commands to GUI components
    app.connect_btn.configure(command=handle_connect)
    app.send_btn.configure(command=handle_send)
    app.bind("<Return>", lambda _: handle_send())

def message_poller(app):
    """Background thread to update GUI when new messages arrive."""
    while True:
        msg = bridge.read_inc_message_queue()
        if msg and not msg.startswith("ERROR:"):
            # Use .after() to safely update GUI from background thread
            app.after(0, lambda m=msg: app.display_message("Peer", m))
        time.sleep(0.2)

if __name__ == "__main__":
    # 1. Start C Bridge
    startup_info = bridge.startup()
    if startup_info:
        # 2. Initialize GUI
        app = messaging_app()
        setup_gui_logic(app)
        
        # 3. Start Poller
        threading.Thread(target=message_poller, args=(app,), daemon=True).start()
        
        # 4. Run App
        try:
            app.mainloop()
        finally:
            bridge.terminate_program()
