# Technical Reference: MaxiMall Furniture Configurator Architecture

This document provides a technical overview of the data-driven showroom booth and replication architecture.

---

## 1. Dynamic catalog & Data-Driven Meshes
The configurator is fully data-driven, storing all furniture products inside a central DataTable (`DT_FurnitureCatalog`) mapped to the `FFurnitureProductRow` structure (defined in [FurnitureTypes.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/Data/FurnitureTypes.h)).

Each furniture product consists of up to seven individual visual subcomponents, which are dynamically applied to corresponding `UStaticMeshComponent` slots at runtime:
1. **Cabinet Body** (base static mesh + material configuration)
2. **Door Slot 0** (left/single door, relative offsets, and material overrides)
3. **Door Slot 1** (right door, relative offsets, and material overrides)
4. **Countertop**
5. **Sink** (visibility depends on Countertop selection type)
6. **Faucet**
7. **Mirror**

---

## 2. Multiplayer Replication Model
Replication is managed in C++ inside the `AShowroomBooth` class ([ShowroomBooth.h](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.h) / [ShowroomBooth.cpp](file:///C:/Users/Admin/Desktop/Aleg/UE5C++/MaxiMall/Source/MaxiMall/FurnitureConfigurator/ShowroomBooth.cpp)):

* **Product ID Tracking**: The active product ID is stored in a replicated `FName ActiveProductID` property:
  ```cpp
  UPROPERTY(ReplicatedUsing = OnRep_ActiveProductID, EditAnywhere, Category = "Showroom | State")
  FName ActiveProductID;
  ```
* **Server Authority RPC**: When a client requests a catalog change, they invoke a Server RPC:
  ```cpp
  UFUNCTION(Server, Reliable, WithValidation)
  void Server_ChangeProductID(FName NewProductID);
  ```
  The server performs validations (e.g., proximity checks, cooldowns, row checks) and updates `ActiveProductID`.
* **Replication RepNotify (`OnRep_ActiveProductID`)**:
  Once updated on the server, `ActiveProductID` replicates down to all clients, triggering their local `OnRep_ActiveProductID()` function. Clients query the local `DT_FurnitureCatalog` and update all mesh slots locally in C++.

---

## 3. Separation of Concerns: Showroom vs. Preview Actor
To keep the main game simulation lightweight and clean:
* **`AShowroomBooth` (Server-Authoritative)**: This actor lives in the level, replicates to all clients, and is seen by all players. It manages interactions, collisions, and gameplay states.
* **`AFurniturePreviewActor` (Client-Local)**: This actor is spawned dynamically **on the owning client only** (`bReplicates = false`). It represents a private local snapshot of a product catalog row, spawned far away from the level. It has zero coupling with character classes or network serialization, ensuring a clean, multiplayer-safe local inspection experience.
